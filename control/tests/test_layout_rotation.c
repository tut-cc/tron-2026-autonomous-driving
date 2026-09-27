#include "dualcore_board.h"
#include "frame_rotation.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
_Static_assert(offsetof(dc_shared_t,control)==128,"control header is cache-line aligned");
_Static_assert(sizeof(dc_shared_t)==784,"both cores use the same shared layout");
int main(void) {
 unsigned char image[]={1,0,2,0,3,0,99,98,4,0,5,0,6,0,97,96};
 const unsigned char expected[]={6,0,5,0,4,0,99,98,3,0,2,0,1,0,97,96};
 assert(frame_rotate_180_rgb565(image,3,2,4,sizeof image)==0);
 assert(memcmp(image,expected,sizeof image)==0);
 assert(frame_rotate_180_rgb565(image,3,2,2,sizeof image)==-1);
 assert(frame_rotate_180_rgb565(image,3,2,4,15)==-1);
 assert(frame_rotate_180_rgb565(NULL,1,1,1,2)==-1);
 unsigned char odd[]={1,2,3,4,5,6};
 assert(frame_rotate_180_rgb565(odd,3,1,3,6)==0);
 assert(odd[0]==5&&odd[1]==6&&odd[2]==3&&odd[3]==4&&odd[4]==1&&odd[5]==2);
 puts("PASS shared ABI (784 bytes / control offset128), 180-degree rotation, stride padding, odd center, invalid bounds");
 return 0;
}
