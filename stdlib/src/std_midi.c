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
#endif

#define MLANG_MIDI_QUEUE_CAPACITY 256u
#define MLANG_MIDI_MAX_MESSAGE_BYTES 1024u

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
    return p_midi_jack_client_close && p_midi_jack_activate &&
                   p_midi_jack_deactivate && p_midi_jack_port_get_buffer &&
                   p_midi_jack_get_event_count && p_midi_jack_event_get &&
                   p_midi_jack_clear_buffer && p_midi_jack_event_write
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
