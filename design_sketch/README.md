Prompt to gemini 3 to create this whole folder: 
"make a new folder called design_sketch. In it add acaffolding for a profesisonal version of the current server. I want to use the scheduler as is (i will extend its features in the future). AI want the server and its listeners to be designed in such a weay so that it is testable. The diffrent listeners should be modules that can be independantly testet.
As for the client i want you to show me how to implement it professionally. Im thinking perhaps a state machine that leeps track of pipelines and filters for each step, easy to test and modular. But you can make whatever design chipses you want. I am doing this as a plan for version a future version 1.0, Also show how to professionally integrate backends, also pipelines perhaps? Metrics and other modules might be needed too.  "

# UB-WeatherServer Design Sketch (v1.0 Plan)

This folder contains the architectural scaffolding for the professional version of the Weather Server.
```c
design_sketch/
├── README.md                  # Architecture documentation
├── include/
│   ├── core/
│   │   ├── server.h           # Main server composition root
│   │   └── context.h          # Request context (tracing, metrics, session)
│   ├── network/
│   │   ├── listener.h         # Abstract listener interface (Testable!)
│   │   └── connection.h       # (Placeholder for connection logic)
│   ├── processing/
│   │   ├── fsm.h              # Generic Finite State Machine for clients
│   │   ├── pipeline.h         # Request processing pipeline
│   │   └── filter.h           # Individual processing steps (Auth, Logic, etc.)
│   ├── backend/
│   │   └── backend_interface.h # Abstract data source interface
│   └── observability/
│       └── metrics.h          # Abstract metrics interface
└── src/
    └── example_usage.c        # Code example showing how to wire it all up
```
## Architecture Overview

The design focuses on **Modularity**, **Testability**, and **Observability**.

### 1. Core (`include/core/`)
- **Server (`server.h`)**: The composition root. It manages the lifecycle of the application, holds the `mj_scheduler`, and orchestrates listeners and backends.
- **Server (`server.h`)**: The composition root. It holds references to global configuration, listeners, backends and the external `mj_scheduler` (the scheduler is the program master). The server registers listeners/tasks with the scheduler but does not run or destroy the scheduler.
- **Context (`context.h`)**: A request-scoped object passed through all layers. It carries the Request ID (for tracing), Metrics recorder, and user session data.

### 2. Network Layer (`include/network/`)
- **Listener (`listener.h`)**: An abstract interface for network listeners.
    - *Benefit*: You can implement a `MockListener` for testing the server logic without opening real sockets.
    - *Benefit*: Supports multiple protocols (HTTP, WebSocket) easily.

### 3. Processing Layer (`include/processing/`)
- **Finite State Machine (`fsm.h`)**:
    - The client connection lifecycle is managed by a generic FSM.
    - States: `INIT` -> `READ_REQUEST` -> `PROCESS` -> `WRITE_RESPONSE` -> `DONE`.
    - *Benefit*: Makes the complex async logic of handling partial reads/writes deterministic and easy to test.
- **Pipeline & Filters (`pipeline.h`, `filter.h`)**:
    - Request processing is broken down into a chain of filters.
    - Example Chain: `AuthFilter` -> `RateLimitFilter` -> `BusinessLogicFilter` -> `CompressionFilter`.
    - *Benefit*: Each filter is a small, independent unit that can be unit-tested in isolation.

### 4. Backend Layer (`include/backend/`)
- **Backend Interface (`backend_interface.h`)**:
    - Abstract interface for data sources.
    - *Benefit*: Allows swapping the real WeatherAPI with a `MockBackend` or `CacheBackend` for testing and development.

### 5. Observability (`include/observability/`)
- **Metrics (`metrics.h`)**:
    - Abstract interface for recording metrics.
    - Decouples the application code from the specific metrics system (Prometheus, StatsD, etc.).

## Testing Strategy

1.  **Unit Tests**:
    *   **Filters**: Create a `pipeline`, add a single filter, pass in a mock `ub_context` and input data, assert output.
    *   **FSM**: Initialize FSM, trigger events, assert state transitions.
2.  **Integration Tests**:
    *   **Server**: Instantiate `ub_server` with a `MockListener` and `MockBackend`. Verify that the server correctly wires them up.

## Implementation Guide

See `src/example_usage.c` (if created) or the headers for API usage.

## Server Lifecycle (recommended)

1. Create the top-level `mj_scheduler` (the program master).
2. Create the `ub_server` using `ub_server_create_with_scheduler(&config, top_sched)`.
3. Register backends and listeners with the server using `ub_server_register_backend()` and `ub_server_add_listener()`.
4. Start the server listeners with `ub_server_start_listeners(server)` — this registers their tasks on `top_sched`.
5. Run the top-level scheduler: `mj_scheduler_run(top_sched)`.
6. After the scheduler exits, call `ub_server_stop_listeners(server)` to stop listeners and then `ub_server_destroy(server)`.
7. Destroy the top-level scheduler when the application decides it's time.
