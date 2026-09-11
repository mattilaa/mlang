#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <errno.h>
#endif

#define MLANG_MIDI_QUEUE_CAPACITY 256u
#define MLANG_MIDI_MAX_MESSAGE_BYTES 1024u
#define MLANG_MIDI_PATCHBAY_MAX_PORTS 32u
#define MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS 128u

typedef struct
{
    uint64_t timestamp;
    uint32_t length;
    unsigned char data[MLANG_MIDI_MAX_MESSAGE_BYTES];
} mlang_midi_packet_t;

typedef struct
{
    mlang_midi_packet_t packets[MLANG_MIDI_QUEUE_CAPACITY];
    _Atomic uint32_t read_index;
    _Atomic uint32_t write_index;
    _Atomic uint64_t dropped;
} mlang_midi_queue_t;

typedef struct mlang_midi_port
{
    int direction; /* 1=input, 2=output */
    int64_t device_id;
    mlang_midi_queue_t input;
    mlang_midi_queue_t output;
#if defined(__APPLE__)
    MIDIClientRef client;
    MIDIPortRef port;
    MIDIEndpointRef endpoint;
#elif defined(__linux__)
    void* jack_lib;
    void* jack_client;
    void* jack_port;
#endif
} mlang_midi_port_t;

typedef struct
{
    mlang_midi_packet_t packet;
} mlang_midi_message_t;

typedef struct mlang_midi_patchbay mlang_midi_patchbay_t;

typedef struct
{
    mlang_midi_patchbay_t* patchbay;
    uint32_t index;
} mlang_midi_patchbay_input_context_t;

typedef struct
{
    int64_t device_id;
    mlang_midi_queue_t queue;
#if defined(__APPLE__)
    MIDIEndpointRef endpoint;
#elif defined(__linux__)
    void* jack_port;
    char remote_name[512];
#endif
} mlang_midi_patchbay_input_t;

typedef struct
{
    int64_t device_id;
    mlang_midi_queue_t queue;
#if defined(__APPLE__)
    MIDIEndpointRef endpoint;
#elif defined(__linux__)
    void* jack_port;
    char remote_name[512];
#endif
} mlang_midi_patchbay_output_t;

typedef struct
{
    uint32_t input;
    uint32_t output;
    _Atomic int active;
} mlang_midi_patchbay_connection_t;

struct mlang_midi_patchbay
{
    _Atomic int started;
    uint32_t input_count;
    uint32_t output_count;
    mlang_midi_patchbay_input_t inputs[MLANG_MIDI_PATCHBAY_MAX_PORTS];
    mlang_midi_patchbay_output_t outputs[MLANG_MIDI_PATCHBAY_MAX_PORTS];
    mlang_midi_patchbay_input_context_t
        input_contexts[MLANG_MIDI_PATCHBAY_MAX_PORTS];
    mlang_midi_patchbay_connection_t
        connections[MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS];
#if defined(__APPLE__)
    MIDIClientRef client;
    MIDIPortRef input_port;
    MIDIPortRef output_port;
#elif defined(__linux__)
    void* jack_lib;
    void* jack_client;
    uint32_t available_input_count;
    uint32_t available_output_count;
    char input_device_names[MLANG_MIDI_PATCHBAY_MAX_PORTS][512];
    char output_device_names[MLANG_MIDI_PATCHBAY_MAX_PORTS][512];
#endif
};

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static char g_midi_last_error[512];
static char g_midi_device_name[512];

static void midi_set_error(const char* message)
{
    (void)snprintf(g_midi_last_error, sizeof(g_midi_last_error), "%s",
                   message ? message : "std::midi: unknown error");
}

static void midi_clear_error(void)
{
    g_midi_last_error[0] = '\0';
}

const char* __mlang_std_midi_last_error(void)
{
    return g_midi_last_error;
}

const char* __mlang_std_midi_backend_name(void)
{
#if defined(__APPLE__)
    return "coremidi";
#elif defined(__linux__)
    return "jack-midi";
#else
    return "unsupported";
#endif
}

static int midi_queue_push(mlang_midi_queue_t* queue, uint64_t timestamp,
                           const unsigned char* data, uint32_t length)
{
    if(!queue || !data || length == 0u || length > MLANG_MIDI_MAX_MESSAGE_BYTES)
        return -1;
    const uint32_t write =
        atomic_load_explicit(&queue->write_index, memory_order_relaxed);
    const uint32_t read =
        atomic_load_explicit(&queue->read_index, memory_order_acquire);
    if((uint32_t)(write - read) >= MLANG_MIDI_QUEUE_CAPACITY)
    {
        (void)atomic_fetch_add_explicit(&queue->dropped, 1u,
                                        memory_order_relaxed);
        return -1;
    }
    mlang_midi_packet_t* packet =
        &queue->packets[write % MLANG_MIDI_QUEUE_CAPACITY];
    packet->timestamp = timestamp;
    packet->length = length;
    memcpy(packet->data, data, length);
    atomic_store_explicit(&queue->write_index, write + 1u,
                          memory_order_release);
    return 0;
}

static int midi_queue_pop(mlang_midi_queue_t* queue,
                          mlang_midi_packet_t* packet)
{
    if(!queue || !packet)
        return -1;
    const uint32_t read =
        atomic_load_explicit(&queue->read_index, memory_order_relaxed);
    const uint32_t write =
        atomic_load_explicit(&queue->write_index, memory_order_acquire);
    if(read == write)
        return 0;
    *packet = queue->packets[read % MLANG_MIDI_QUEUE_CAPACITY];
    atomic_store_explicit(&queue->read_index, read + 1u, memory_order_release);
    return 1;
}

static int midi_validate_bytes(mlang_list_t bytes, unsigned char* output,
                               uint32_t* output_length)
{
    if(bytes.size <= 0 || bytes.size > MLANG_MIDI_MAX_MESSAGE_BYTES ||
       !bytes.data)
    {
        midi_set_error("std::midi message must contain 1..1024 bytes");
        return -1;
    }
    const int32_t* input = (const int32_t*)bytes.data;
    for(int64_t i = 0; i < bytes.size; ++i)
    {
        if(input[i] < 0 || input[i] > 255)
        {
            midi_set_error("std::midi message byte is outside 0..255");
            return -1;
        }
        output[i] = (unsigned char)input[i];
    }
    *output_length = (uint32_t)bytes.size;
    return 0;
}

#if defined(__APPLE__)

static MIDIEndpointRef midi_apple_endpoint(int direction, int64_t device_id)
{
    if(device_id < 0)
        return 0;
    return direction == 1 ? MIDIGetSource((ItemCount)device_id)
                          : MIDIGetDestination((ItemCount)device_id);
}

static void midi_apple_read(const MIDIPacketList* packets, void* context,
                            void* source_context)
{
    (void)source_context;
    mlang_midi_port_t* port = (mlang_midi_port_t*)context;
    if(!port || !packets)
        return;
    const MIDIPacket* packet = &packets->packet[0];
    for(UInt32 i = 0; i < packets->numPackets; ++i)
    {
        uint32_t length = packet->length;
        if(length > MLANG_MIDI_MAX_MESSAGE_BYTES)
            length = MLANG_MIDI_MAX_MESSAGE_BYTES;
        (void)midi_queue_push(&port->input, (uint64_t)packet->timeStamp,
                              packet->data, length);
        packet = MIDIPacketNext(packet);
    }
}

static void midi_apple_patchbay_read(const MIDIPacketList* packets,
                                     void* context, void* source_context)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)context;
    mlang_midi_patchbay_input_context_t* input_context =
        (mlang_midi_patchbay_input_context_t*)source_context;
    if(!patchbay || !input_context || input_context->patchbay != patchbay ||
       input_context->index >= patchbay->input_count || !packets ||
       !atomic_load_explicit(&patchbay->started, memory_order_acquire))
        return;

    const uint32_t input_index = input_context->index;
    const MIDIPacket* packet = &packets->packet[0];
    for(UInt32 i = 0; i < packets->numPackets; ++i)
    {
        uint32_t length = packet->length;
        if(length > MLANG_MIDI_MAX_MESSAGE_BYTES)
            length = MLANG_MIDI_MAX_MESSAGE_BYTES;
        (void)midi_queue_push(&patchbay->inputs[input_index].queue,
                              (uint64_t)packet->timeStamp, packet->data,
                              length);
        packet = MIDIPacketNext(packet);
    }

    for(uint32_t i = 0; i < MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS; ++i)
    {
        const mlang_midi_patchbay_connection_t* connection =
            &patchbay->connections[i];
        if(atomic_load_explicit(&connection->active, memory_order_acquire) &&
           connection->input == input_index &&
           connection->output < patchbay->output_count)
        {
            (void)MIDISend(patchbay->output_port,
                           patchbay->outputs[connection->output].endpoint,
                           packets);
        }
    }
}

static int64_t midi_apple_count(int direction)
{
    return direction == 1 ? (int64_t)MIDIGetNumberOfSources()
                          : (int64_t)MIDIGetNumberOfDestinations();
}

static const char* midi_apple_name(int direction, int64_t device_id)
{
    const int64_t count = midi_apple_count(direction);
    if(device_id < 0 || device_id >= count)
    {
        midi_set_error("std::midi CoreMIDI device id is invalid");
        return "";
    }
    MIDIEndpointRef endpoint = midi_apple_endpoint(direction, device_id);
    CFStringRef name = NULL;
    if(!endpoint ||
       MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name) !=
           noErr ||
       !name)
    {
        midi_set_error("std::midi CoreMIDI device name lookup failed");
        return "";
    }
    const Boolean ok =
        CFStringGetCString(name, g_midi_device_name, sizeof(g_midi_device_name),
                           kCFStringEncodingUTF8);
    CFRelease(name);
    if(!ok)
    {
        midi_set_error("std::midi CoreMIDI device name is not UTF-8");
        return "";
    }
    midi_clear_error();
    return g_midi_device_name;
}

static int midi_apple_open(mlang_midi_port_t* port, const char* client_name)
{
    CFStringRef client = CFStringCreateWithCString(
        NULL, client_name && client_name[0] ? client_name : "mlang_midi",
        kCFStringEncodingUTF8);
    if(!client)
    {
        midi_set_error("std::midi CoreMIDI client name allocation failed");
        return -1;
    }
    OSStatus rc = MIDIClientCreate(client, NULL, NULL, &port->client);
    CFRelease(client);
    if(rc != noErr)
    {
        midi_set_error("std::midi CoreMIDI MIDIClientCreate failed");
        return -1;
    }
    CFStringRef port_name = CFSTR("mlang MIDI port");
    if(port->direction == 1)
    {
        rc = MIDIInputPortCreate(port->client, port_name, midi_apple_read, port,
                                 &port->port);
        if(rc == noErr)
            rc = MIDIPortConnectSource(port->port, port->endpoint, NULL);
    }
    else
    {
        rc = MIDIOutputPortCreate(port->client, port_name, &port->port);
    }
    if(rc != noErr)
    {
        midi_set_error(port->direction == 1
                           ? "std::midi CoreMIDI input port open failed"
                           : "std::midi CoreMIDI output port open failed");
        return -1;
    }
    return 0;
}

#elif defined(__linux__)

typedef uint32_t jack_nframes_t;
typedef uint32_t jack_status_t;
typedef struct
{
    jack_nframes_t time;
    size_t size;
    unsigned char* buffer;
} mlang_jack_midi_event_t;

#define MLANG_JACK_NO_START_SERVER 1u
#define MLANG_JACK_PORT_IS_INPUT 1u
#define MLANG_JACK_PORT_IS_OUTPUT 2u
#define MLANG_JACK_DEFAULT_MIDI_TYPE "8 bit raw midi"

typedef void* (*midi_jack_client_open_fn)(const char*, uint32_t, jack_status_t*,
                                          ...);
typedef int (*midi_jack_client_close_fn)(void*);
typedef int (*midi_jack_activate_fn)(void*);
typedef int (*midi_jack_deactivate_fn)(void*);
typedef void* (*midi_jack_port_register_fn)(void*, const char*, const char*,
                                            unsigned long, unsigned long);
typedef int (*midi_jack_set_process_callback_fn)(void*,
                                                 int (*)(jack_nframes_t, void*),
                                                 void*);
typedef void* (*midi_jack_port_get_buffer_fn)(void*, jack_nframes_t);
typedef uint32_t (*midi_jack_get_event_count_fn)(void*);
typedef int (*midi_jack_event_get_fn)(mlang_jack_midi_event_t*, void*,
                                      uint32_t);
typedef void (*midi_jack_clear_buffer_fn)(void*);
typedef int (*midi_jack_event_write_fn)(void*, jack_nframes_t,
                                        const unsigned char*, size_t);
typedef const char** (*midi_jack_get_ports_fn)(void*, const char*, const char*,
                                               unsigned long);
typedef const char* (*midi_jack_port_name_fn)(const void*);
typedef int (*midi_jack_connect_fn)(void*, const char*, const char*);
typedef void (*midi_jack_free_fn)(void*);

static midi_jack_client_close_fn p_midi_jack_client_close;
static midi_jack_activate_fn p_midi_jack_activate;
static midi_jack_deactivate_fn p_midi_jack_deactivate;
static midi_jack_port_get_buffer_fn p_midi_jack_port_get_buffer;
static midi_jack_get_event_count_fn p_midi_jack_get_event_count;
static midi_jack_event_get_fn p_midi_jack_event_get;
static midi_jack_clear_buffer_fn p_midi_jack_clear_buffer;
static midi_jack_event_write_fn p_midi_jack_event_write;
static midi_jack_get_ports_fn p_midi_jack_get_ports;
static midi_jack_port_name_fn p_midi_jack_port_name;
static midi_jack_connect_fn p_midi_jack_connect;
static midi_jack_free_fn p_midi_jack_free;

static void* midi_jack_library(void)
{
    void* library = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
    if(!library)
        library = dlopen("libjack.so", RTLD_NOW | RTLD_LOCAL);
    return library;
}

static int midi_jack_process(jack_nframes_t frames, void* context)
{
    mlang_midi_port_t* port = (mlang_midi_port_t*)context;
    if(!port || !p_midi_jack_port_get_buffer)
        return 0;
    void* buffer = p_midi_jack_port_get_buffer(port->jack_port, frames);
    if(!buffer)
        return 0;
    if(port->direction == 1)
    {
        const uint32_t count = p_midi_jack_get_event_count
                                   ? p_midi_jack_get_event_count(buffer)
                                   : 0u;
        for(uint32_t i = 0; i < count; ++i)
        {
            mlang_jack_midi_event_t event;
            if(p_midi_jack_event_get &&
               p_midi_jack_event_get(&event, buffer, i) == 0 && event.buffer)
            {
                uint32_t length = event.size > MLANG_MIDI_MAX_MESSAGE_BYTES
                                      ? MLANG_MIDI_MAX_MESSAGE_BYTES
                                      : (uint32_t)event.size;
                (void)midi_queue_push(&port->input, event.time, event.buffer,
                                      length);
            }
        }
    }
    else
    {
        if(p_midi_jack_clear_buffer)
            p_midi_jack_clear_buffer(buffer);
        mlang_midi_packet_t packet;
        while(midi_queue_pop(&port->output, &packet) == 1)
        {
            if(p_midi_jack_event_write)
                (void)p_midi_jack_event_write(buffer, 0, packet.data,
                                              packet.length);
        }
    }
    return 0;
}

static int midi_jack_patchbay_process(jack_nframes_t frames, void* context)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)context;
    if(!patchbay ||
       !atomic_load_explicit(&patchbay->started, memory_order_acquire) ||
       !p_midi_jack_port_get_buffer)
        return 0;

    void* output_buffers[MLANG_MIDI_PATCHBAY_MAX_PORTS] = {0};
    for(uint32_t output = 0; output < patchbay->output_count; ++output)
    {
        output_buffers[output] = p_midi_jack_port_get_buffer(
            patchbay->outputs[output].jack_port, frames);
        if(output_buffers[output] && p_midi_jack_clear_buffer)
            p_midi_jack_clear_buffer(output_buffers[output]);

        mlang_midi_packet_t queued;
        while(output_buffers[output] &&
              midi_queue_pop(&patchbay->outputs[output].queue, &queued) == 1)
        {
            if(p_midi_jack_event_write)
                (void)p_midi_jack_event_write(output_buffers[output], 0,
                                              queued.data, queued.length);
        }
    }

    for(uint32_t input = 0; input < patchbay->input_count; ++input)
    {
        void* input_buffer = p_midi_jack_port_get_buffer(
            patchbay->inputs[input].jack_port, frames);
        const uint32_t count = input_buffer && p_midi_jack_get_event_count
                                   ? p_midi_jack_get_event_count(input_buffer)
                                   : 0u;
        for(uint32_t event_index = 0; event_index < count; ++event_index)
        {
            mlang_jack_midi_event_t event;
            if(!p_midi_jack_event_get ||
               p_midi_jack_event_get(&event, input_buffer, event_index) != 0 ||
               !event.buffer)
                continue;
            const uint32_t length = event.size > MLANG_MIDI_MAX_MESSAGE_BYTES
                                        ? MLANG_MIDI_MAX_MESSAGE_BYTES
                                        : (uint32_t)event.size;
            (void)midi_queue_push(&patchbay->inputs[input].queue, event.time,
                                  event.buffer, length);
            for(uint32_t route = 0; route < MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS;
                ++route)
            {
                const mlang_midi_patchbay_connection_t* connection =
                    &patchbay->connections[route];
                if(atomic_load_explicit(&connection->active,
                                        memory_order_acquire) &&
                   connection->input == input &&
                   connection->output < patchbay->output_count &&
                   output_buffers[connection->output] &&
                   p_midi_jack_event_write)
                {
                    (void)p_midi_jack_event_write(
                        output_buffers[connection->output], event.time,
                        event.buffer, length);
                }
            }
        }
    }
    return 0;
}

static int midi_jack_symbols(void* library)
{
    p_midi_jack_client_close =
        (midi_jack_client_close_fn)dlsym(library, "jack_client_close");
    p_midi_jack_activate =
        (midi_jack_activate_fn)dlsym(library, "jack_activate");
    p_midi_jack_deactivate =
        (midi_jack_deactivate_fn)dlsym(library, "jack_deactivate");
    p_midi_jack_port_get_buffer =
        (midi_jack_port_get_buffer_fn)dlsym(library, "jack_port_get_buffer");
    p_midi_jack_get_event_count = (midi_jack_get_event_count_fn)dlsym(
        library, "jack_midi_get_event_count");
    p_midi_jack_event_get =
        (midi_jack_event_get_fn)dlsym(library, "jack_midi_event_get");
    p_midi_jack_clear_buffer =
        (midi_jack_clear_buffer_fn)dlsym(library, "jack_midi_clear_buffer");
    p_midi_jack_event_write =
        (midi_jack_event_write_fn)dlsym(library, "jack_midi_event_write");
    p_midi_jack_get_ports =
        (midi_jack_get_ports_fn)dlsym(library, "jack_get_ports");
    p_midi_jack_port_name =
        (midi_jack_port_name_fn)dlsym(library, "jack_port_name");
    p_midi_jack_connect = (midi_jack_connect_fn)dlsym(library, "jack_connect");
    p_midi_jack_free = (midi_jack_free_fn)dlsym(library, "jack_free");
    return p_midi_jack_client_close && p_midi_jack_activate &&
                   p_midi_jack_deactivate && p_midi_jack_port_get_buffer &&
                   p_midi_jack_get_event_count && p_midi_jack_event_get &&
                   p_midi_jack_clear_buffer && p_midi_jack_event_write &&
                   p_midi_jack_get_ports && p_midi_jack_port_name &&
                   p_midi_jack_connect && p_midi_jack_free
               ? 0
               : -1;
}

static int midi_jack_open(mlang_midi_port_t* port, const char* client_name)
{
    port->jack_lib = midi_jack_library();
    if(!port->jack_lib)
    {
        midi_set_error("std::midi JACK libjack not found; install JACK first");
        return -1;
    }
    midi_jack_client_open_fn client_open =
        (midi_jack_client_open_fn)dlsym(port->jack_lib, "jack_client_open");
    midi_jack_port_register_fn port_register =
        (midi_jack_port_register_fn)dlsym(port->jack_lib, "jack_port_register");
    midi_jack_set_process_callback_fn set_process =
        (midi_jack_set_process_callback_fn)dlsym(port->jack_lib,
                                                 "jack_set_process_callback");
    midi_jack_get_ports_fn get_ports =
        (midi_jack_get_ports_fn)dlsym(port->jack_lib, "jack_get_ports");
    midi_jack_port_name_fn port_name =
        (midi_jack_port_name_fn)dlsym(port->jack_lib, "jack_port_name");
    midi_jack_connect_fn connect =
        (midi_jack_connect_fn)dlsym(port->jack_lib, "jack_connect");
    midi_jack_free_fn jack_free =
        (midi_jack_free_fn)dlsym(port->jack_lib, "jack_free");
    if(!client_open || !port_register || !set_process || !get_ports ||
       !port_name || !connect || midi_jack_symbols(port->jack_lib) != 0)
    {
        midi_set_error("std::midi JACK MIDI symbols are unavailable");
        return -1;
    }
    jack_status_t status = 0;
    port->jack_client =
        client_open(client_name && client_name[0] ? client_name : "mlang_midi",
                    MLANG_JACK_NO_START_SERVER, &status);
    if(!port->jack_client)
    {
        midi_set_error("std::midi JACK client open failed; is jackd running?");
        return -1;
    }
    const unsigned long local_flags = port->direction == 1
                                          ? MLANG_JACK_PORT_IS_INPUT
                                          : MLANG_JACK_PORT_IS_OUTPUT;
    port->jack_port = port_register(
        port->jack_client, port->direction == 1 ? "midi_in" : "midi_out",
        MLANG_JACK_DEFAULT_MIDI_TYPE, local_flags, 0);
    if(!port->jack_port ||
       set_process(port->jack_client, midi_jack_process, port) != 0 ||
       p_midi_jack_activate(port->jack_client) != 0)
    {
        midi_set_error("std::midi JACK MIDI port activation failed");
        return -1;
    }
    const unsigned long remote_flags = port->direction == 1
                                           ? MLANG_JACK_PORT_IS_OUTPUT
                                           : MLANG_JACK_PORT_IS_INPUT;
    const char** ports = get_ports(port->jack_client, NULL,
                                   MLANG_JACK_DEFAULT_MIDI_TYPE, remote_flags);
    const char* local_name = port_name(port->jack_port);
    if(!ports || !ports[port->device_id] || !local_name)
    {
        if(ports && jack_free)
            jack_free((void*)ports);
        midi_set_error("std::midi JACK MIDI device id is unavailable");
        return -1;
    }
    const int rc =
        port->direction == 1
            ? connect(port->jack_client, ports[port->device_id], local_name)
            : connect(port->jack_client, local_name, ports[port->device_id]);
    if(ports && jack_free)
        jack_free((void*)ports);
    if(rc != 0)
    {
        midi_set_error("std::midi JACK MIDI port connection failed");
        return -1;
    }
    return 0;
}

static int64_t midi_jack_count_or_name(int direction, int64_t requested,
                                       int want_name)
{
    void* library = midi_jack_library();
    if(!library)
    {
        midi_set_error("std::midi JACK libjack not found; install JACK first");
        return want_name ? -1 : 0;
    }
    midi_jack_client_open_fn open_client =
        (midi_jack_client_open_fn)dlsym(library, "jack_client_open");
    midi_jack_client_close_fn close_client =
        (midi_jack_client_close_fn)dlsym(library, "jack_client_close");
    midi_jack_get_ports_fn get_ports =
        (midi_jack_get_ports_fn)dlsym(library, "jack_get_ports");
    midi_jack_free_fn jack_free =
        (midi_jack_free_fn)dlsym(library, "jack_free");
    if(!open_client || !close_client || !get_ports)
    {
        midi_set_error("std::midi JACK MIDI query symbols are unavailable");
        dlclose(library);
        return want_name ? -1 : 0;
    }
    jack_status_t status = 0;
    void* client =
        open_client("mlang_midi_query", MLANG_JACK_NO_START_SERVER, &status);
    if(!client)
    {
        midi_set_error("std::midi JACK client open failed; is jackd running?");
        dlclose(library);
        return want_name ? -1 : 0;
    }
    const unsigned long flags =
        direction == 1 ? MLANG_JACK_PORT_IS_OUTPUT : MLANG_JACK_PORT_IS_INPUT;
    const char** ports =
        get_ports(client, NULL, MLANG_JACK_DEFAULT_MIDI_TYPE, flags);
    int64_t count = 0;
    if(ports)
        while(ports[count])
            ++count;
    if(want_name && requested >= 0 && requested < count)
        (void)snprintf(g_midi_device_name, sizeof(g_midi_device_name), "%s",
                       ports[requested]);
    if(ports && jack_free)
        jack_free((void*)ports);
    close_client(client);
    dlclose(library);
    midi_clear_error();
    return want_name ? (requested >= 0 && requested < count ? 0 : -1) : count;
}

#endif

int64_t __mlang_std_midi_input_count(void)
{
#if defined(__APPLE__)
    return midi_apple_count(1);
#elif defined(__linux__)
    return midi_jack_count_or_name(1, -1, 0);
#else
    return 0;
#endif
}

int64_t __mlang_std_midi_output_count(void)
{
#if defined(__APPLE__)
    return midi_apple_count(2);
#elif defined(__linux__)
    return midi_jack_count_or_name(2, -1, 0);
#else
    return 0;
#endif
}

int64_t __mlang_std_midi_default_input_id(void)
{
    return __mlang_std_midi_input_count() > 0 ? 0 : -1;
}

int64_t __mlang_std_midi_default_output_id(void)
{
    return __mlang_std_midi_output_count() > 0 ? 0 : -1;
}

const char* __mlang_std_midi_input_name(int64_t device_id)
{
#if defined(__APPLE__)
    return midi_apple_name(1, device_id);
#elif defined(__linux__)
    if(midi_jack_count_or_name(1, device_id, 1) != 0)
    {
        midi_set_error(
            "std::midi JACK MIDI input id is invalid or JACK is unavailable");
        return "";
    }
    midi_clear_error();
    return g_midi_device_name;
#else
    (void)device_id;
    midi_set_error("std::midi input is unsupported on this platform");
    return "";
#endif
}

const char* __mlang_std_midi_output_name(int64_t device_id)
{
#if defined(__APPLE__)
    return midi_apple_name(2, device_id);
#elif defined(__linux__)
    if(midi_jack_count_or_name(2, device_id, 1) != 0)
    {
        midi_set_error(
            "std::midi JACK MIDI output id is invalid or JACK is unavailable");
        return "";
    }
    midi_clear_error();
    return g_midi_device_name;
#else
    (void)device_id;
    midi_set_error("std::midi output is unsupported on this platform");
    return "";
#endif
}

static int64_t midi_open(int direction, int64_t device_id,
                         const char* client_name)
{
    midi_clear_error();
    const int64_t count = direction == 1 ? __mlang_std_midi_input_count()
                                         : __mlang_std_midi_output_count();
    if(device_id < 0)
        device_id = count > 0 ? 0 : -1;
    if(device_id < 0 || device_id >= count)
    {
        if(g_midi_last_error[0] == '\0')
            midi_set_error(direction == 1
                               ? "std::midi has no selectable input device"
                               : "std::midi has no selectable output device");
        return 0;
    }
    mlang_midi_port_t* port = (mlang_midi_port_t*)calloc(1, sizeof(*port));
    if(!port)
    {
        midi_set_error("std::midi port allocation failed");
        return 0;
    }
    port->direction = direction;
    port->device_id = device_id;
#if defined(__APPLE__)
    port->endpoint = midi_apple_endpoint(direction, device_id);
    if(!port->endpoint || midi_apple_open(port, client_name) != 0)
    {
        if(port->port)
            MIDIPortDispose(port->port);
        if(port->client)
            MIDIClientDispose(port->client);
        free(port);
        return 0;
    }
#elif defined(__linux__)
    if(midi_jack_open(port, client_name) != 0)
    {
        if(port->jack_client && p_midi_jack_client_close)
            p_midi_jack_client_close(port->jack_client);
        if(port->jack_lib)
            dlclose(port->jack_lib);
        free(port);
        return 0;
    }
#else
    (void)client_name;
    free(port);
    midi_set_error("std::midi is unsupported on this platform");
    return 0;
#endif
    midi_clear_error();
    return (int64_t)(intptr_t)port;
}

int64_t __mlang_std_midi_open_input(int64_t device_id, const char* client_name)
{
    return midi_open(1, device_id, client_name);
}

int64_t __mlang_std_midi_open_output(int64_t device_id, const char* client_name)
{
    return midi_open(2, device_id, client_name);
}

int32_t __mlang_std_midi_close(int64_t handle)
{
    mlang_midi_port_t* port = (mlang_midi_port_t*)(intptr_t)handle;
    if(!port)
        return -1;
#if defined(__APPLE__)
    if(port->direction == 1 && port->port && port->endpoint)
        (void)MIDIPortDisconnectSource(port->port, port->endpoint);
    if(port->port)
        (void)MIDIPortDispose(port->port);
    if(port->client)
        (void)MIDIClientDispose(port->client);
#elif defined(__linux__)
    if(port->jack_client && p_midi_jack_deactivate)
        (void)p_midi_jack_deactivate(port->jack_client);
    if(port->jack_client && p_midi_jack_client_close)
        (void)p_midi_jack_client_close(port->jack_client);
    if(port->jack_lib)
        dlclose(port->jack_lib);
#endif
    free(port);
    return 0;
}

int64_t __mlang_std_midi_poll(int64_t handle)
{
    mlang_midi_port_t* port = (mlang_midi_port_t*)(intptr_t)handle;
    if(!port || port->direction != 1)
    {
        midi_set_error("std::midi poll requires an input port");
        return 0;
    }
    mlang_midi_message_t* message =
        (mlang_midi_message_t*)malloc(sizeof(*message));
    if(!message)
    {
        midi_set_error("std::midi message allocation failed");
        return 0;
    }
    const int rc = midi_queue_pop(&port->input, &message->packet);
    if(rc != 1)
    {
        free(message);
        return 0;
    }
    return (int64_t)(intptr_t)message;
}

int64_t __mlang_std_midi_dropped_messages(int64_t handle)
{
    mlang_midi_port_t* port = (mlang_midi_port_t*)(intptr_t)handle;
    return port ? (int64_t)atomic_load_explicit(&port->input.dropped,
                                                memory_order_relaxed)
                : -1;
}

int64_t __mlang_std_midi_send_bytes(int64_t handle, mlang_list_t bytes)
{
    mlang_midi_port_t* port = (mlang_midi_port_t*)(intptr_t)handle;
    if(!port || port->direction != 2)
    {
        midi_set_error("std::midi send requires an output port");
        return -1;
    }
    unsigned char data[MLANG_MIDI_MAX_MESSAGE_BYTES];
    uint32_t length = 0;
    if(midi_validate_bytes(bytes, data, &length) != 0)
        return -1;
#if defined(__APPLE__)
    const size_t storage_size = sizeof(MIDIPacketList) + length + 32u;
    MIDIPacketList* list = (MIDIPacketList*)malloc(storage_size);
    if(!list)
    {
        midi_set_error("std::midi CoreMIDI packet allocation failed");
        return -1;
    }
    MIDIPacket* packet = MIDIPacketListInit(list);
    packet = MIDIPacketListAdd(list, storage_size, packet, 0, length, data);
    const OSStatus rc =
        packet ? MIDISend(port->port, port->endpoint, list) : -1;
    free(list);
    if(rc != noErr)
    {
        midi_set_error("std::midi CoreMIDI send failed");
        return -1;
    }
#elif defined(__linux__)
    if(midi_queue_push(&port->output, 0, data, length) != 0)
    {
        midi_set_error("std::midi JACK output queue is full");
        return -1;
    }
#else
    midi_set_error("std::midi output is unsupported on this platform");
    return -1;
#endif
    midi_clear_error();
    return length;
}

int64_t __mlang_std_midi_patchbay_open(const char* client_name)
{
    midi_clear_error();
    mlang_midi_patchbay_t* patchbay =
        (mlang_midi_patchbay_t*)calloc(1, sizeof(*patchbay));
    if(!patchbay)
    {
        midi_set_error("std::midi patchbay allocation failed");
        return 0;
    }
#if defined(__APPLE__)
    CFStringRef name = CFStringCreateWithCString(
        NULL, client_name && client_name[0] ? client_name : "mlang_patchbay",
        kCFStringEncodingUTF8);
    OSStatus rc =
        name ? MIDIClientCreate(name, NULL, NULL, &patchbay->client) : -1;
    if(name)
        CFRelease(name);
    if(rc == noErr)
        rc = MIDIInputPortCreate(patchbay->client, CFSTR("patchbay inputs"),
                                 midi_apple_patchbay_read, patchbay,
                                 &patchbay->input_port);
    if(rc == noErr)
        rc = MIDIOutputPortCreate(patchbay->client, CFSTR("patchbay outputs"),
                                  &patchbay->output_port);
    if(rc != noErr)
    {
        if(patchbay->input_port)
            (void)MIDIPortDispose(patchbay->input_port);
        if(patchbay->output_port)
            (void)MIDIPortDispose(patchbay->output_port);
        if(patchbay->client)
            (void)MIDIClientDispose(patchbay->client);
        free(patchbay);
        midi_set_error("std::midi CoreMIDI patchbay open failed");
        return 0;
    }
#elif defined(__linux__)
    patchbay->jack_lib = midi_jack_library();
    if(!patchbay->jack_lib || midi_jack_symbols(patchbay->jack_lib) != 0)
    {
        if(patchbay->jack_lib)
            dlclose(patchbay->jack_lib);
        free(patchbay);
        midi_set_error("std::midi JACK patchbay requires libjack MIDI support");
        return 0;
    }
    midi_jack_client_open_fn open_client =
        (midi_jack_client_open_fn)dlsym(patchbay->jack_lib, "jack_client_open");
    midi_jack_set_process_callback_fn set_process =
        (midi_jack_set_process_callback_fn)dlsym(patchbay->jack_lib,
                                                 "jack_set_process_callback");
    jack_status_t status = 0;
    patchbay->jack_client =
        open_client
            ? open_client(client_name && client_name[0] ? client_name
                                                        : "mlang_patchbay",
                          MLANG_JACK_NO_START_SERVER, &status)
            : NULL;
    if(!patchbay->jack_client || !set_process ||
       set_process(patchbay->jack_client, midi_jack_patchbay_process,
                   patchbay) != 0)
    {
        if(patchbay->jack_client && p_midi_jack_client_close)
            (void)p_midi_jack_client_close(patchbay->jack_client);
        dlclose(patchbay->jack_lib);
        free(patchbay);
        midi_set_error(
            "std::midi JACK patchbay open failed; is jackd running?");
        return 0;
    }

    const char** ports = p_midi_jack_get_ports(patchbay->jack_client, NULL,
                                               MLANG_JACK_DEFAULT_MIDI_TYPE,
                                               MLANG_JACK_PORT_IS_OUTPUT);
    while(ports &&
          patchbay->available_input_count < MLANG_MIDI_PATCHBAY_MAX_PORTS &&
          ports[patchbay->available_input_count])
    {
        const uint32_t index = patchbay->available_input_count++;
        (void)snprintf(patchbay->input_device_names[index],
                       sizeof(patchbay->input_device_names[index]), "%s",
                       ports[index]);
    }
    if(ports && p_midi_jack_free)
        p_midi_jack_free((void*)ports);
    ports = p_midi_jack_get_ports(patchbay->jack_client, NULL,
                                  MLANG_JACK_DEFAULT_MIDI_TYPE,
                                  MLANG_JACK_PORT_IS_INPUT);
    while(ports &&
          patchbay->available_output_count < MLANG_MIDI_PATCHBAY_MAX_PORTS &&
          ports[patchbay->available_output_count])
    {
        const uint32_t index = patchbay->available_output_count++;
        (void)snprintf(patchbay->output_device_names[index],
                       sizeof(patchbay->output_device_names[index]), "%s",
                       ports[index]);
    }
    if(ports && p_midi_jack_free)
        p_midi_jack_free((void*)ports);
#else
    (void)client_name;
    free(patchbay);
    midi_set_error("std::midi patchbay is unsupported on this platform");
    return 0;
#endif
    midi_clear_error();
    return (int64_t)(intptr_t)patchbay;
}

static int midi_patchbay_can_configure(mlang_midi_patchbay_t* patchbay)
{
    if(!patchbay)
    {
        midi_set_error("std::midi patchbay handle is invalid");
        return 0;
    }
    if(atomic_load_explicit(&patchbay->started, memory_order_acquire))
    {
        midi_set_error(
            "std::midi patchbay must be stopped before reconfiguration");
        return 0;
    }
    return 1;
}

int64_t __mlang_std_midi_patchbay_add_input(int64_t handle, int64_t device_id,
                                            const char* port_name)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!midi_patchbay_can_configure(patchbay))
        return -1;
    if(patchbay->input_count >= MLANG_MIDI_PATCHBAY_MAX_PORTS)
    {
        midi_set_error("std::midi patchbay input limit is 32");
        return -1;
    }
    if(device_id < 0)
        device_id = __mlang_std_midi_default_input_id();
#if defined(__APPLE__)
    if(device_id < 0 || device_id >= __mlang_std_midi_input_count())
    {
        midi_set_error("std::midi patchbay input device id is invalid");
        return -1;
    }
#elif defined(__linux__)
    if(device_id < 0 || device_id >= (int64_t)patchbay->available_input_count)
    {
        midi_set_error("std::midi patchbay JACK input device id is invalid");
        return -1;
    }
#else
    (void)port_name;
    midi_set_error("std::midi patchbay input is unsupported");
    return -1;
#endif
    const uint32_t index = patchbay->input_count;
    mlang_midi_patchbay_input_t* input = &patchbay->inputs[index];
    input->device_id = device_id;
#if defined(__APPLE__)
    (void)port_name;
    input->endpoint = midi_apple_endpoint(1, device_id);
    patchbay->input_contexts[index].patchbay = patchbay;
    patchbay->input_contexts[index].index = index;
    if(!input->endpoint ||
       MIDIPortConnectSource(patchbay->input_port, input->endpoint,
                             &patchbay->input_contexts[index]) != noErr)
    {
        midi_set_error("std::midi CoreMIDI patchbay input connection failed");
        return -1;
    }
#elif defined(__linux__)
    midi_jack_port_register_fn register_port =
        (midi_jack_port_register_fn)dlsym(patchbay->jack_lib,
                                          "jack_port_register");
    char fallback[32];
    (void)snprintf(fallback, sizeof(fallback), "input_%u", index);
    input->jack_port =
        register_port
            ? register_port(patchbay->jack_client,
                            port_name && port_name[0] ? port_name : fallback,
                            MLANG_JACK_DEFAULT_MIDI_TYPE,
                            MLANG_JACK_PORT_IS_INPUT, 0)
            : NULL;
    if(!input->jack_port)
    {
        midi_set_error("std::midi JACK patchbay input registration failed");
        return -1;
    }
    (void)snprintf(input->remote_name, sizeof(input->remote_name), "%s",
                   patchbay->input_device_names[device_id]);
#endif
    ++patchbay->input_count;
    midi_clear_error();
    return index;
}

int64_t __mlang_std_midi_patchbay_add_output(int64_t handle, int64_t device_id,
                                             const char* port_name)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!midi_patchbay_can_configure(patchbay))
        return -1;
    if(patchbay->output_count >= MLANG_MIDI_PATCHBAY_MAX_PORTS)
    {
        midi_set_error("std::midi patchbay output limit is 32");
        return -1;
    }
    if(device_id < 0)
        device_id = __mlang_std_midi_default_output_id();
#if defined(__APPLE__)
    if(device_id < 0 || device_id >= __mlang_std_midi_output_count())
    {
        midi_set_error("std::midi patchbay output device id is invalid");
        return -1;
    }
#elif defined(__linux__)
    if(device_id < 0 || device_id >= (int64_t)patchbay->available_output_count)
    {
        midi_set_error("std::midi patchbay JACK output device id is invalid");
        return -1;
    }
#else
    (void)port_name;
    midi_set_error("std::midi patchbay output is unsupported");
    return -1;
#endif
    const uint32_t index = patchbay->output_count;
    mlang_midi_patchbay_output_t* output = &patchbay->outputs[index];
    output->device_id = device_id;
#if defined(__APPLE__)
    (void)port_name;
    output->endpoint = midi_apple_endpoint(2, device_id);
    if(!output->endpoint)
    {
        midi_set_error("std::midi CoreMIDI patchbay output is unavailable");
        return -1;
    }
#elif defined(__linux__)
    midi_jack_port_register_fn register_port =
        (midi_jack_port_register_fn)dlsym(patchbay->jack_lib,
                                          "jack_port_register");
    char fallback[32];
    (void)snprintf(fallback, sizeof(fallback), "output_%u", index);
    output->jack_port =
        register_port
            ? register_port(patchbay->jack_client,
                            port_name && port_name[0] ? port_name : fallback,
                            MLANG_JACK_DEFAULT_MIDI_TYPE,
                            MLANG_JACK_PORT_IS_OUTPUT, 0)
            : NULL;
    if(!output->jack_port)
    {
        midi_set_error("std::midi JACK patchbay output registration failed");
        return -1;
    }
    (void)snprintf(output->remote_name, sizeof(output->remote_name), "%s",
                   patchbay->output_device_names[device_id]);
#endif
    ++patchbay->output_count;
    midi_clear_error();
    return index;
}

int32_t __mlang_std_midi_patchbay_connect(int64_t handle, int64_t input,
                                          int64_t output)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!midi_patchbay_can_configure(patchbay))
        return -1;
    if(input < 0 || input >= patchbay->input_count || output < 0 ||
       output >= patchbay->output_count)
    {
        midi_set_error("std::midi patchbay connection port id is invalid");
        return -1;
    }
    mlang_midi_patchbay_connection_t* free_connection = NULL;
    for(uint32_t i = 0; i < MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS; ++i)
    {
        mlang_midi_patchbay_connection_t* connection =
            &patchbay->connections[i];
        if(atomic_load_explicit(&connection->active, memory_order_acquire))
        {
            if(connection->input == (uint32_t)input &&
               connection->output == (uint32_t)output)
                return 0;
        }
        else if(!free_connection)
        {
            free_connection = connection;
        }
    }
    if(!free_connection)
    {
        midi_set_error("std::midi patchbay connection limit is 128");
        return -1;
    }
    free_connection->input = (uint32_t)input;
    free_connection->output = (uint32_t)output;
    atomic_store_explicit(&free_connection->active, 1, memory_order_release);
    midi_clear_error();
    return 0;
}

int32_t __mlang_std_midi_patchbay_disconnect(int64_t handle, int64_t input,
                                             int64_t output)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!midi_patchbay_can_configure(patchbay))
        return -1;
    for(uint32_t i = 0; i < MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS; ++i)
    {
        mlang_midi_patchbay_connection_t* connection =
            &patchbay->connections[i];
        if(atomic_load_explicit(&connection->active, memory_order_acquire) &&
           connection->input == (uint32_t)input &&
           connection->output == (uint32_t)output)
        {
            atomic_store_explicit(&connection->active, 0, memory_order_release);
            midi_clear_error();
            return 0;
        }
    }
    midi_set_error("std::midi patchbay connection does not exist");
    return -1;
}

int64_t __mlang_std_midi_patchbay_connection_count(int64_t handle)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay)
        return -1;
    int64_t count = 0;
    for(uint32_t i = 0; i < MLANG_MIDI_PATCHBAY_MAX_CONNECTIONS; ++i)
        if(atomic_load_explicit(&patchbay->connections[i].active,
                                memory_order_acquire))
            ++count;
    return count;
}

int64_t __mlang_std_midi_patchbay_input_count(int64_t handle)
{
    const mlang_midi_patchbay_t* patchbay =
        (const mlang_midi_patchbay_t*)(intptr_t)handle;
    return patchbay ? patchbay->input_count : -1;
}

int64_t __mlang_std_midi_patchbay_output_count(int64_t handle)
{
    const mlang_midi_patchbay_t* patchbay =
        (const mlang_midi_patchbay_t*)(intptr_t)handle;
    return patchbay ? patchbay->output_count : -1;
}

int32_t __mlang_std_midi_patchbay_start(int64_t handle)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay)
    {
        midi_set_error("std::midi patchbay handle is invalid");
        return -1;
    }
    if(atomic_load_explicit(&patchbay->started, memory_order_acquire))
        return 0;
#if defined(__linux__)
    if(!p_midi_jack_activate ||
       p_midi_jack_activate(patchbay->jack_client) != 0)
    {
        midi_set_error("std::midi JACK patchbay activation failed");
        return -1;
    }
    for(uint32_t i = 0; i < patchbay->input_count; ++i)
    {
        const char* local =
            p_midi_jack_port_name(patchbay->inputs[i].jack_port);
        const int rc =
            local ? p_midi_jack_connect(patchbay->jack_client,
                                        patchbay->inputs[i].remote_name, local)
                  : -1;
        if(rc != 0 && rc != EEXIST)
        {
            (void)p_midi_jack_deactivate(patchbay->jack_client);
            midi_set_error("std::midi JACK patchbay input connection failed");
            return -1;
        }
    }
    for(uint32_t i = 0; i < patchbay->output_count; ++i)
    {
        const char* local =
            p_midi_jack_port_name(patchbay->outputs[i].jack_port);
        const int rc =
            local ? p_midi_jack_connect(patchbay->jack_client, local,
                                        patchbay->outputs[i].remote_name)
                  : -1;
        if(rc != 0 && rc != EEXIST)
        {
            (void)p_midi_jack_deactivate(patchbay->jack_client);
            midi_set_error("std::midi JACK patchbay output connection failed");
            return -1;
        }
    }
#elif !defined(__APPLE__)
    midi_set_error("std::midi patchbay is unsupported on this platform");
    return -1;
#endif
    atomic_store_explicit(&patchbay->started, 1, memory_order_release);
    midi_clear_error();
    return 0;
}

int32_t __mlang_std_midi_patchbay_stop(int64_t handle)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay)
        return -1;
    if(!atomic_exchange_explicit(&patchbay->started, 0, memory_order_acq_rel))
        return 0;
#if defined(__linux__)
    if(p_midi_jack_deactivate && patchbay->jack_client)
        (void)p_midi_jack_deactivate(patchbay->jack_client);
#endif
    return 0;
}

int64_t __mlang_std_midi_patchbay_poll(int64_t handle, int64_t input)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay || input < 0 || input >= patchbay->input_count)
    {
        midi_set_error("std::midi patchbay poll input id is invalid");
        return 0;
    }
    mlang_midi_message_t* message =
        (mlang_midi_message_t*)malloc(sizeof(*message));
    if(!message)
    {
        midi_set_error("std::midi patchbay message allocation failed");
        return 0;
    }
    if(midi_queue_pop(&patchbay->inputs[input].queue, &message->packet) != 1)
    {
        free(message);
        return 0;
    }
    return (int64_t)(intptr_t)message;
}

int64_t __mlang_std_midi_patchbay_dropped_messages(int64_t handle,
                                                   int64_t input)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay || input < 0 || input >= patchbay->input_count)
        return -1;
    return (int64_t)atomic_load_explicit(&patchbay->inputs[input].queue.dropped,
                                         memory_order_relaxed);
}

int64_t __mlang_std_midi_patchbay_send_bytes(int64_t handle, int64_t output,
                                             mlang_list_t bytes)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay || output < 0 || output >= patchbay->output_count)
    {
        midi_set_error("std::midi patchbay send output id is invalid");
        return -1;
    }
    if(!atomic_load_explicit(&patchbay->started, memory_order_acquire))
    {
        midi_set_error("std::midi patchbay must be started before sending");
        return -1;
    }
    unsigned char data[MLANG_MIDI_MAX_MESSAGE_BYTES];
    uint32_t length = 0;
    if(midi_validate_bytes(bytes, data, &length) != 0)
        return -1;
#if defined(__APPLE__)
    const size_t storage_size = sizeof(MIDIPacketList) + length + 32u;
    MIDIPacketList* list = (MIDIPacketList*)malloc(storage_size);
    if(!list)
    {
        midi_set_error("std::midi CoreMIDI patchbay packet allocation failed");
        return -1;
    }
    MIDIPacket* packet = MIDIPacketListInit(list);
    packet = MIDIPacketListAdd(list, storage_size, packet, 0, length, data);
    const OSStatus rc = packet
                            ? MIDISend(patchbay->output_port,
                                       patchbay->outputs[output].endpoint, list)
                            : -1;
    free(list);
    if(rc != noErr)
    {
        midi_set_error("std::midi CoreMIDI patchbay send failed");
        return -1;
    }
#elif defined(__linux__)
    if(midi_queue_push(&patchbay->outputs[output].queue, 0, data, length) != 0)
    {
        midi_set_error("std::midi JACK patchbay output queue is full");
        return -1;
    }
#else
    midi_set_error("std::midi patchbay output is unsupported");
    return -1;
#endif
    midi_clear_error();
    return length;
}

int32_t __mlang_std_midi_patchbay_close(int64_t handle)
{
    mlang_midi_patchbay_t* patchbay = (mlang_midi_patchbay_t*)(intptr_t)handle;
    if(!patchbay)
        return -1;
    (void)__mlang_std_midi_patchbay_stop(handle);
#if defined(__APPLE__)
    for(uint32_t i = 0; i < patchbay->input_count; ++i)
        if(patchbay->inputs[i].endpoint)
            (void)MIDIPortDisconnectSource(patchbay->input_port,
                                           patchbay->inputs[i].endpoint);
    if(patchbay->input_port)
        (void)MIDIPortDispose(patchbay->input_port);
    if(patchbay->output_port)
        (void)MIDIPortDispose(patchbay->output_port);
    if(patchbay->client)
        (void)MIDIClientDispose(patchbay->client);
#elif defined(__linux__)
    if(patchbay->jack_client && p_midi_jack_client_close)
        (void)p_midi_jack_client_close(patchbay->jack_client);
    if(patchbay->jack_lib)
        dlclose(patchbay->jack_lib);
#endif
    free(patchbay);
    return 0;
}

int64_t __mlang_std_midi_message_timestamp(int64_t handle)
{
    const mlang_midi_message_t* message =
        (const mlang_midi_message_t*)(intptr_t)handle;
    return message ? (int64_t)message->packet.timestamp : -1;
}

int64_t __mlang_std_midi_message_len(int64_t handle)
{
    const mlang_midi_message_t* message =
        (const mlang_midi_message_t*)(intptr_t)handle;
    return message ? (int64_t)message->packet.length : -1;
}

int32_t __mlang_std_midi_message_byte(int64_t handle, int64_t index)
{
    const mlang_midi_message_t* message =
        (const mlang_midi_message_t*)(intptr_t)handle;
    if(!message || index < 0 || index >= message->packet.length)
        return -1;
    return message->packet.data[index];
}

int32_t __mlang_std_midi_message_close(int64_t handle)
{
    mlang_midi_message_t* message = (mlang_midi_message_t*)(intptr_t)handle;
    if(!message)
        return -1;
    free(message);
    return 0;
}
