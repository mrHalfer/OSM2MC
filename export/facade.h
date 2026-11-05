#pragma once
// The facade marking of kindergartens/schools/research institutes (moved out
// of mcworldexporter.cpp): repaints the wall blocks of one built object.
#include "export/voxelstore.h"
#include <QtGlobal>

void decorateWalls(VoxelStore& st, int code, quint16 wallId);
