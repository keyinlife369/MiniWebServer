# MiniWebServer

A high-performance, multi-threaded HTTP/HTTPS web server written in modern C++20, featuring a Qt6 GUI monitor, MySQL integration, RESTful API routing, and Docker support.

## Features

- **HTTP/HTTPS Dual Port** — serves plain HTTP and TLS-encrypted HTTPS simultaneously
- **Async I/O** — built on Boost.Asio with a configurable thread pool for concurrent request handling
- **URL Router** — exact-match (O(1) hash) and parameterized paths (`/api/user/:id`) with route params injection
- **Full HTTP Method Support** — GET, HEAD, POST, PUT, DELETE
- **HTTP Pipelining** — incremental body reception and keep-alive with precise buffer management
- **MySQL 9.5 Integration** — C API wrapper with RAII connection pool, auto-reconnect, and SQL injection protection
- **RESTful API** — built-in CRUD endpoints for a guestbook/message board
- **Form Parser** — URL-encoded form data and query string parsing
- **Qt6 GUI Monitor** — native desktop window showing real-time server stats (threads, connections)
- **Docker** — multi-stage build (builder → minimal runtime) + docker-compose for MySQL
- **Unit Tests** — 48 tests across HTTP parser, router, thread pool, and form parser (Google Test)
- **Benchmark Tool** — built-in concurrent HTTP load generator with QPS/latency metrics
- **10 MB Body Limit** — configurable max request body size

## Architecture

```
                    ┌─────────────────────────────┐
                    │        Qt6 MainWindow        │
                    │     (monitor & control)      │
                    └──────────────┬──────────────┘
                                   │
              ┌────────────────────┴────────────────────┐
              │              WebServer                   │
              │  ┌──────────────────────────────────┐   │
              │  │        Boost.Asio io_context      │   │
              │  │   ┌──────────┐  ┌──────────────┐ │   │
              │  │   │ HTTP     │  │  HTTPS       │ │   │
              │  │   │ acceptor │  │  acceptor    │ │   │
              │  │   │ :8080    │  │  :8443       │ │   │
              │  │   └────┬─────┘  └──────┬───────┘ │   │
              │  │        │               │          │   │
              │  │   ┌────▼─────┐  ┌──────▼───────┐ │   │
              │  │   │Connection│  │ SslConnection│ │   │
              │  │   │ (plain)  │  │ (TLS stream) │ │   │
              │  │   └────┬─────┘  └──────┬───────┘ │   │
              │  └────────┼───────────────┼─────────┘   │
              │           │               │              │
              │      ┌────▼─────┐  ┌──────▼───────┐     │
              │      │ HttpParser│  │   Router     │     │
              │      └────┬─────┘  └──────┬───────┘     │
              │           │               │              │
              │      ┌────▼───────────────▼───────┐      │
              │      │       ThreadPool           │      │
              │      │   (N × worker threads)     │      │
              │      └────────────┬───────────────┘      │
              │                   │                      │
              │      ┌────────────▼───────────────┐      │
              │      │  Route Handlers / Static   │      │
              │      │  ┌────────┐ ┌───────────┐ │      │
              │      │  │MysqlPool│ │ File Serve│ │      │
              │      │  │ (8 conn)│ │ (static/) │ │      │
              │      │  └────────┘ └───────────┘ │      │
              │      └───────────────────────────┘      │
              └──────────────────────────────────────────┘
```

## Requirements

| Dependency | Version | Required For |
|---|---|---|
| C++ Compiler | MSVC 2022 / GCC 13+ / Clang 18+ | C++20 support |
| CMake | ≥ 3.16 | Build system |
| Boost | ≥ 1.75 | Asio networking |
| Qt6 | ≥ 6.2 (Core, Widgets, Network) | GUI + base |
| MySQL | 9.5 (C API) | Database (optional at runtime) |
| OpenSSL | ≥ 3.0 | TLS/SSL |
| Google Test | 1.15.2 | Unit tests (auto-fetched) |

### Windows

```bash
# Boost — set BOOST_ROOT env var or pass -DBOOST_ROOT=...
# Qt6 — install via Qt Online Installer
# MySQL — install MySQL Community Server 9.5
# OpenSSL — install via vcpkg:
vcpkg install openssl:x64-windows
```

### Ubuntu 24.04

```bash
apt install -y build-essential cmake ninja-build \
    libboost-all-dev libssl-dev libmysqlclient-dev \
    qt6-base-dev libqt6network6
```

## Build

```bash
git clone <repo-url> && cd MiniWebServer

# Configure
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBOOST_ROOT=/path/to/boost \
    -DMYSQL_DIR=/path/to/mysql \
    -DOPENSSL_ROOT_DIR=/path/to/openssl

# Build
cmake --build build -j$(nproc)

# Run (from build directory)
./MiniWebServer

# Build without tests/bench
cmake -B build -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF
```

## Usage

### Starting the Server

```
MiniWebServer.exe [port] [ssl_port] [cert_file] [key_file]
```

The Qt6 monitor window opens on launch — it displays real-time connection counts, thread pool utilization, and request throughput. Close the window to shut down the server gracefully.

### RESTful API

| Method | Path | Description |
|---|---|---|
| `POST` | `/api/messages` | Create a message (form: `author` & `content`) |
| `GET` | `/api/messages` | List recent messages (JSON array) |
| `GET` | `/api/message/:id` | Get a single message |
| `DELETE` | `/api/message/:id` | Delete a message |
| `POST` | `/api/echo` | Echo form data back (testing) |
| `GET` | `/api/info` | Server info endpoint |

### Static Files

Place files under `static/` — they are served at the root path (`/index.html`, `/style.css`, etc.). Default MIME types are resolved from file extensions. `GET /` maps to `/index.html`.

### Benchmark Tool

```bash
MiniWebServer_bench.exe <host> <port> <path> <connections> <requests_per_conn>

# Example: 100 concurrent connections × 100 requests = 10,000 total
MiniWebServer_bench.exe localhost 8080 /api/info 100 100
```

Outputs: total time, success/fail counts, bytes transferred, QPS, throughput, and average latency.

## Docker

### Build & Run (standalone)

```bash
docker build -t miniwebserver .
docker run -p 8080:8080 -p 8443:8443 miniwebserver
```

### With MySQL (docker-compose)

```bash
# Start MySQL first
docker-compose up -d mysql

# Then build and run the server
docker build -t miniwebserver .
docker run --network host miniwebserver
```

On first start, `sql/init.sql` auto-creates the `miniwebserver` database and `messages` table.

## Project Structure

```
MiniWebServer/
├── CMakeLists.txt              # Build configuration
├── Dockerfile                  # Multi-stage container build
├── docker-compose.yml          # MySQL 9.5 service
├── README.md
│
├── include/
│   ├── server.h                # WebServer + Connection + SslConnection
│   ├── http_parser.h           # HTTP/1.1 request parser (state machine)
│   ├── thread_pool.h           # Generic work-stealing thread pool
│   ├── router.h                # URL router + HttpResponse
│   ├── mysql_wrapper.h         # MySQL RAII connection + pool
│   ├── form_parser.h           # URL-encoded form parser
│   ├── server_monitor.h        # Server statistics collector
│   ├── mainwindow.h            # Qt6 monitor window
│   └── logger.h                # Thread-safe logging
│
├── src/
│   ├── main.cpp                # Entry point + HTML page generation
│   ├── mainwindow.cpp          # Qt6 GUI + route registration
│   ├── server.cpp              # Core server: accept, read/write, request processing
│   ├── http_parser.cpp         # HTTP state machine implementation
│   ├── router.cpp              # Route matching engine
│   ├── mysql_wrapper.cpp       # MySQL C API wrapper + connection pool
│   ├── form_parser.cpp         # Form data parsing
│   └── server_monitor.cpp      # Real-time metrics
│
├── static/
│   └── index.html              # Default landing page
│
├── test/
│   ├── test_main.cpp           # GTest entry
│   ├── test_http_parser.cpp    # 12 tests
│   ├── test_thread_pool.cpp    # 8 tests
│   ├── test_form_parser.cpp    # 14 tests
│   └── test_router.cpp         # 13 tests
│
├── bench/
│   └── bench_client.cpp        # HTTP load generator
│
├── sql/
│   └── init.sql                # Database bootstrap script
│
└── certs/
    ├── server.crt              # Self-signed TLS certificate (RSA 4096)
    └── server.key              # Private key
```

## Running Tests

```bash
# Build with tests enabled (default ON)
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# Run with CTest
cd build && ctest --output-on-failure

# Or run directly
./MiniWebServer_tests
```

### Test Coverage

| Module | Tests | Covers |
|---|---|---|
| HTTP Parser | 12 | All 5 methods, chunked body, size limit, header trim, pipelining, keep-alive |
| Thread Pool | 8 | Enqueue, concurrency, out-of-order proof, destructor join, stop exception |
| Form Parser | 14 | URL decode, `+`→space, `%` encoding, UTF-8, edge cases, multi key-value |
| Router | 13 | Exact match, param capture, method filter, fallback, dispatch, all HTTP verbs |

## Design Decisions

- **MySQL C API over Connector/C++** — lighter dependency footprint, higher learning value
- **`shared_mutex` for routes** — write-lock only during startup registration; read-only at runtime (zero contention)
- **`dynamic_pointer_cast` for SSL detection** — single `processRequest()` handles both HTTP and HTTPS, avoiding code duplication
- **Exact-first route matching** — O(1) hash lookup before O(n) parameter scan; allows `/api/user/admin` to override `/api/user/:id`
- **Multi-stage Docker build** — builder image (~800 MB) with full toolchain; runtime image (~200 MB) with only needed libraries
- **RAII connection guard** — pool connections auto-return on scope exit, preventing leaks
---

*Built with C++20, Boost.Asio, Qt6, OpenSSL, and MySQL.*
