#ifndef GLBFILECACHEHANDLER_H
#define GLBFILECACHEHANDLER_H

#include "../../core/FileCacheHandlers/filecachehandler.h"

class GlbFileCacheHandler : public FileCacheHandler {
    e_OBJECT
protected:
    GlbFileCacheHandler();

    void reload() override;
public:
    void replace() override;
};

#endif // GLBFILECACHEHANDLER_H
