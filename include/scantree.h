/* scantree.h: read a library folder into an FsNode tree (read-only). */
#ifndef THC_SCANTREE_H
#define THC_SCANTREE_H

#include "fstree.h"
#include "settings.h"

/* Scan `path`. NULL if it is not a folder. A folder equal to `skip` (may be NULL) is left out wherever it lies below
 * `path`, so the NSFW folder inside a library folder is read once, as its own root (#34). Folders with nothing
 * THC can show are dropped. */
FsNode *scan_tree(const char *path, const char *skip);

#endif
