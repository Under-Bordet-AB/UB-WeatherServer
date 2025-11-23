# C Naming Standard

This document defines naming conventions for C projects to ensure clarity, consistency, long-term maintainability, and safe integration into large modular codebases.

It uses:

* **snake_case** for variables and functions
* **PascalCase** for typedefs and structures
* **ALL_CAPS** for macros
* **Hierarchical function names** for clear module structure
* **`static`** to mark internal/private functions
* **`#pragma once`** for header guards
* **No `_t` types** to avoid POSIX-reserved suffixes

---

# 1. General Principles

* Names must be **clear**, **descriptive**, and **consistent**.
* Avoid obscure abbreviations; use common short forms (`len`, `idx`, `buf`).
* Do **not** introduce new `_t` types (reserved by POSIX for system typedefs).
* Prefer readability over brevity — descriptive names reduce maintenance cost.
* Module and subsystem hierarchy should be visible directly in names.

---

# 2. Variables

**Style:** `snake_case`

Examples:

```c
count
buffer_size
is_ready
current_index
```

### Rules

* Always lowercase with underscores.
* Boolean variables should read naturally (e.g., `is_valid`, `has_error`).
* Loop counters may be short (`i`, `j`, `k`) when used locally and obviously.

---

# 3. Functions

**Style:** `snake_case` with **hierarchical prefixing**
`module_subsystem_action()`

Examples:

```c
net_connection_init()
net_connection_send()
net_connection_receive()
net_connection_dispose()

ui_window_open()
ui_window_close()
```

### Rules

* Names should start with a **verb** (`init`, `open`, `send`, `read`, `update`, etc.).
* Long hierarchical names are preferred for clarity in medium/large projects.
* Functions that operate on the same conceptual object share a prefix.

---

# 4. Internal / Private Functions

**Use the `static` keyword**, not naming tricks.

```c
static int parse_header(Parser *p);
static void update_state(Session *s);
```

`static` gives the function **internal linkage**, preventing external visibility.

Do **not** use a leading underscore to indicate private functions —
this does **not** restrict visibility and may conflict with reserved identifiers.

---

# 5. Types

**Style:** `PascalCase`
Do **not** use a `_t` suffix (reserved by POSIX).

Examples:

```c
typedef struct FileInfo {
    char *path;
    size_t size;
} FileInfo;

typedef enum Color {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE
} Color;
```

### Struct & Enum Declaration Options

Both are acceptable when used consistently:

**Unified name:**

```c
typedef struct Device {
    int id;
} Device;
```

**Separate tag:**

```c
struct device { int id; };
typedef struct device Device;
```

---

# 6. Macros and Constants

**Style:** `ALL_CAPS_WITH_UNDERSCORES`

Examples:

```c
#define MAX_BUFFER_SIZE 4096
#define DEFAULT_TIMEOUT_MS 500
#define ENABLE_LOGGING 1
```

Prefer `static const` when the value has type significance.

---

# 7. Global Variables

**Style:** `snake_case` with module prefix**

Examples:

```c
int net_default_port;
RendererState renderer_state;
```

Globals should be rare and descriptive.

---

# 8. Header Files

**Preferred:**

```c
#pragma once
```

* Supported by all modern compilers (GCC, Clang, MSVC, ICC, TCC).
* Faster and less error-prone than manual include guards.
* Eliminates risk of include guard name collisions.

If legacy compatibility is required:

```c
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
...
#endif
```

---

# 9. Directory and File Naming

**Style:** `snake_case`

Examples:

```
network/
  net_connection.h
  net_connection.c

filesystem/
  fs_path.h
  fs_path.c
```

File names should reflect modules.

---

# 10. Pointer Naming

Use `_ptr` only when pointer-ness matters:

```c
char *data_ptr;
Node *node_ptr;
```

Otherwise, prefer natural names:

```c
char *buffer;
Connection *conn;
```

---

# 11. Error Handling

### Return Codes

* Functions return `0` on success, nonzero on failure unless documented otherwise.

### Error Enumerations

```c
typedef enum ErrorCode {
    ERR_OK = 0,
    ERR_INVALID_PARAM,
    ERR_OUT_OF_MEMORY,
    ERR_IO_FAIL,
} ErrorCode;
```

Consistent error handling greatly simplifies debugging.

---

# 12. Hierarchical Naming Example (Recommended Style)

```c
// net_connection.h
#pragma once

typedef struct NetConnection {
    int socket_fd;
    char *remote_address;
} NetConnection;

NetConnection *net_connection_init(const char *address, int port);
int net_connection_send(NetConnection *conn, const void *data, size_t len);
int net_connection_receive(NetConnection *conn, void *buffer, size_t len);
void net_connection_dispose(NetConnection *conn);
```

```c
// net_connection.c
#include "net_connection.h"

static int net_connection_open_socket(NetConnection *conn);  // internal

NetConnection *net_connection_init(const char *address, int port) {
    // ...
}

static int net_connection_open_socket(NetConnection *conn) {
    // internal only
}
```

Hierarchy is immediately visible:

```
net_connection_*
```

---

# 13. Summary (Cheat Sheet)

| Category           | Style                       |
| ------------------ | --------------------------- |
| Variables          | `snake_case`                |
| Functions          | `module_subsystem_action()` |
| Internal functions | `static`                    |
| Types              | `PascalCase` (no `_t`)      |
| Macros             | `ALL_CAPS`                  |
| Headers            | `#pragma once`              |
| Files              | `snake_case.[ch]`           |

---