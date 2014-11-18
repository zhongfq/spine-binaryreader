//
// $id: SkeletonBinary.h 2014-08-06 zhongfengqu $
//

#ifndef __SKELETONBINARY_H__
#define __SKELETONBINARY_H__

#include "spine/spine.h"

#ifdef __cplusplus
extern "C" {
#endif

spSkeletonData* spSkeletonBinary_readSkeletonData(const char* skeketonPath, spAtlas* atlas, float scale);

#ifdef __cplusplus
}
#endif

#endif
