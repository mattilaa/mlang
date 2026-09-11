# std::midi

Module file: `stdlib/std/midi.mla`

Portable MIDI 1.0 input and output:

- macOS uses CoreMIDI sources and destinations.
- Linux uses hardware and software JACK MIDI ports. `libjack` is loaded at runtime, and a
  JACK server must be running when a port is opened.

Open ports are active immediately. The native backend uses bounded lock-free
queues between MIDI callbacks and MLang code. Input queue overflow is visible
through `dropped_messages()`; JACK output returns an error when its queue is
full.

## Types

- `midi_input`
- `midi_output`
- `midi_message`
- `midi_patchbay`

## Device discovery

- `backend_name() -> str8`
- `last_error() -> str8`
- `input_count() -> i64`
- `output_count() -> i64`
- `default_input_id() -> i64`
- `default_output_id() -> i64`
- `input_name(device_id: i64) -> str8`
- `output_name(device_id: i64) -> str8`

## Input

- `midi_input::open(device_id, client_name) -> result<midi_input, str8>`
- `midi_input::open_default(client_name) -> result<midi_input, str8>`
- `midi_input::poll() -> option<midi_message>`
- `midi_input::dropped_messages() -> i64`
- `midi_input::close() -> i32`

`poll()` is nonblocking. Each returned message owns a native packet and must be
closed after reading it.

## Output

- `midi_output::open(device_id, client_name) -> result<midi_output, str8>`
- `midi_output::open_default(client_name) -> result<midi_output, str8>`
- `midi_output::send_bytes(bytes) -> result<i64, str8>`
- `midi_output::send(status, data1, data2) -> result<i64, str8>`
- `midi_output::note_on(channel, note, velocity) -> result<i64, str8>`
- `midi_output::note_off(channel, note, velocity) -> result<i64, str8>`
- `midi_output::control_change(channel, controller, value) -> result<i64, str8>`
- `midi_output::program_change(channel, program) -> result<i64, str8>`
- `midi_output::pitch_bend(channel, value) -> result<i64, str8>`
- `midi_output::close() -> i32`

Channels use `0..15`, ordinary data bytes use `0..127`, and pitch bend uses
`0..16383`. `send_bytes` also supports system and SysEx packets from 1 through
1024 bytes, validating that each list element fits in one byte.

## Received messages

- `midi_message::timestamp() -> i64`
- `midi_message::len() -> i64`
- `midi_message::byte(index) -> i32`
- `midi_message::status() -> i32`
- `midi_message::channel() -> i32`
- `midi_message::data1() -> i32`
- `midi_message::data2() -> i32`
- `midi_message::is_note_on() -> bool`
- `midi_message::is_note_off() -> bool`
- `midi_message::close() -> i32`

The timestamp is backend-native: CoreMIDI host time on macOS and the JACK
buffer frame offset on Linux.

## Patchbay

`midi_patchbay` owns one CoreMIDI or JACK client and attaches multiple devices
to it. Each attached device receives a patchbay-local integer port id. Routes
forward packets inside the native MIDI callback, while every input remains
available to `poll()` for monitoring.

- `midi_patchbay::open(client_name) -> result<midi_patchbay, str8>`
- `midi_patchbay::add_input(device_id, port_name) -> result<i64, str8>`
- `midi_patchbay::add_output(device_id, port_name) -> result<i64, str8>`
- `midi_patchbay::connect(input, output) -> result<i32, str8>`
- `midi_patchbay::disconnect(input, output) -> result<i32, str8>`
- `midi_patchbay::input_port_count() -> i64`
- `midi_patchbay::output_port_count() -> i64`
- `midi_patchbay::connection_count() -> i64`
- `midi_patchbay::start() -> result<i32, str8>`
- `midi_patchbay::stop() -> i32`
- `midi_patchbay::poll(input) -> option<midi_message>`
- `midi_patchbay::dropped_messages(input) -> i64`
- `midi_patchbay::send_bytes(output, bytes) -> result<i64, str8>`
- `midi_patchbay::send(output, status, data1, data2) -> result<i64, str8>`
- `midi_patchbay::note_on(...)`, `note_off(...)`, `control_change(...)`,
  `program_change(...)`, and `pitch_bend(...)`
- `midi_patchbay::close() -> i32`

Configure ports and routes while stopped, then call `start()`. Call `stop()`
before adding or removing routes. A single input can fan out to several
outputs, and several inputs can target the same output. The current bounded
implementation supports 32 inputs, 32 outputs, and 128 routes per patchbay.

```rust
let opened: result<MidiPatchbay, str8> = MidiPatchbay::open("router");
let patchbay: MidiPatchbay = opened.unwrap();
let keyboard: i64 = patchbay.add_input(0, "keyboard").unwrap();
let synth: i64 = patchbay.add_output(0, "synth").unwrap();
let monitor: i64 = patchbay.add_output(1, "monitor").unwrap();
patchbay.connect(keyboard, synth);
patchbay.connect(keyboard, monitor);
patchbay.start();
```

## Build and run the demo

```sh
cmake --build build --target mlang mlang_std
./build/mlang examples/std_midi_demo.mla -L ./build -lmlang_std -o build/std_midi_demo
./build/std_midi_demo --list
./build/std_midi_demo --listen --input 0 --duration 5000
./build/std_midi_demo --send --output 0 --note 60 --velocity 96
./build/mlang examples/std_midi_patchbay_demo.mla -L ./build -lmlang_std -o build/std_midi_patchbay_demo
./build/std_midi_patchbay_demo --input 0 --output-a 0 --output-b 1
./build/mlang test tests/std_midi_tests.mla
```

On Debian/Ubuntu Linux, install and start JACK before opening ports, for
example with `sudo apt install jackd2 libjack-jackd2-0` and your preferred
JACK server configuration. The MLang build itself does not require JACK
development headers.
