/* third_party.c: implementations of the vendored single-file libraries
 * (public domain / MIT, see third_party/). Built without THC's warnings. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
