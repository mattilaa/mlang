# protocol_mt examples

Advanced framed TCP protocol demo isolated under one directory:

- `server.mla`: multithreaded server, one worker thread per client.
- `client.mla`: multi-client load generator using protocol frames.
- `run_demo.sh`: builds both examples and runs a full local roundtrip.

## Quick start

From repository root:

```sh
./examples/protocol_mt/run_demo.sh
```

Optional tuning:

```sh
PORT=19111 CLIENTS=2 ROUNDS=5 DELAY_MIN_MS=500 DELAY_MAX_MS=1000 ./examples/protocol_mt/run_demo.sh
```

Default script settings are tuned for a demo run around 5 seconds:
- `CLIENTS=1`
- `ROUNDS=7`
- `DELAY_MIN_MS=500`
- `DELAY_MAX_MS=1000`

Manual run in separate terminals:

```sh
./build/mlang examples/protocol_mt/server.mla -o /tmp/protocol_mt_server
/tmp/protocol_mt_server --port 19095 --clients 4 --rounds 3
```

```sh
./build/mlang examples/protocol_mt/client.mla -o /tmp/protocol_mt_client
/tmp/protocol_mt_client --port 19095 --clients 4 --rounds 3
```
