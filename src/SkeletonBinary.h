//
// $id: SkeletonBinary.h https://github.com/zhongfq/spine-binaryreader $
//

#ifndef __SKELETONBINARY_H__
#define __SKELETONBINARY_H__

#include "spine/spine.h"

#ifdef __cplusplus
extern "C" {
#endif

spSkeletonData *spSkeletonBinary_readSkeletonData(const char *skeketonPath, spAttachmentLoader *attachmentLoader, float scale);

#ifdef __cplusplus
}
#endif

#endif
