## Goal (what we want):
```C
Image *img = load_image("cat.png");
```
and have the correct backend (PNG backend, JPEG backend, etc.) chosen automatically at runtime.

## Example:
#### main.c
```C
#include "image_loader.h"
#include <stdio.h>

int main() {
    Image *a = load_image("cat.png");
    Image *b = load_image("dog.jpeg");
    Image *c = load_image("data.bmp");

    destroy_image(a);
    destroy_image(b);
    destroy_image(c);

    return 0;
}
```

#### output:
```yaml
Loaded PNG: cat.png
Loaded JPEG: dog.jpeg
No backend for file: data.bmp
Destroy PNG image
Destroy JPEG image
```

## Implementation:
### Step 1 — Define the “interface” (the abstract backend)
Let’s define a common interface all backends must implement.
#### image_backend.h
```C
#ifndef IMAGE_BACKEND_H
#define IMAGE_BACKEND_H

#include <stdio.h>

typedef struct Image Image;
typedef struct ImageBackend ImageBackend;

// The interface: functions every backend must implement
struct ImageBackend {
    const char *name;
    int  (*can_load)(const char *filename);
    Image *(*load)(const char *filename);
    void (*destroy)(Image *img);
};

// Simple image struct
struct Image {
    int width;
    int height;
    unsigned char *pixels;
    const ImageBackend *backend;  // which backend created this
};

#endif
```
---
### Step 2 — Define a few backend implementations
Let’s mock up a PNG and JPEG loader (no real image decoding, just stubs).
#### png_backend.c
```C
#include "image_backend.h"
#include <string.h>
#include <stdlib.h>

static int png_can_load(const char *filename) {
    const char *ext = strrchr(filename, '.');
    return ext && strcmp(ext, ".png") == 0;
}

static Image *png_load(const char *filename) {
    Image *img = malloc(sizeof(Image));
    img->width = 800;
    img->height = 600;
    img->pixels = NULL;
    img->backend = &png_backend;
    printf("Loaded PNG: %s\n", filename);
    return img;
}

static void png_destroy(Image *img) {
    printf("Destroy PNG image\n");
    free(img);
}

// Expose as a global instance
const ImageBackend png_backend = {
    .name = "png",
    .can_load = png_can_load,
    .load = png_load,
    .destroy = png_destroy
};
```
#### jpeg_backend.c
```C
#include "image_backend.h"
#include <string.h>
#include <stdlib.h>

static int jpeg_can_load(const char *filename) {
    const char *ext = strrchr(filename, '.');
    return ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0);
}

static Image *jpeg_load(const char *filename) {
    Image *img = malloc(sizeof(Image));
    img->width = 1024;
    img->height = 768;
    img->pixels = NULL;
    img->backend = &jpeg_backend;
    printf("Loaded JPEG: %s\n", filename);
    return img;
}

static void jpeg_destroy(Image *img) {
    printf("Destroy JPEG image\n");
    free(img);
}

const ImageBackend jpeg_backend = {
    .name = "jpeg",
    .can_load = jpeg_can_load,
    .load = jpeg_load,
    .destroy = jpeg_destroy
};
```
---
### Step 3 — Frontend that dispatches to the correct backend
#### image_loader.h
```C
#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "image_backend.h"

Image *load_image(const char *filename);
void destroy_image(Image *img);

#endif
```
#### image_loader.c
```C
#include "image_loader.h"

Image *load_image(const char *filename);
void destroy_image(Image *img);

// Declare available backends
extern const ImageBackend png_backend;
extern const ImageBackend jpeg_backend;

static const ImageBackend *backends[] = {
    &png_backend,
    &jpeg_backend,
    NULL
};

Image *load_image(const char *filename) {
    for (int i = 0; backends[i]; ++i) {
        if (backends[i]->can_load(filename)) {
            return backends[i]->load(filename);
        }
    }
    printf("No backend for file: %s\n", filename);
    return NULL;
}

void destroy_image(Image *img) {
    if (img && img->backend && img->backend->destroy)
        img->backend->destroy(img);
}
```
