# apov-gu-navigator
```
### APoV PSP Navigator Using pspgu
See atomic-point-of-view project to generate the space.

For this gu version, space-block-size should be 256. Example:
    ./bin/apov space-block-size:256 vertical-pov-count:1 horizontal-pov-count:90 \
        ray-step:4 max-ray-depth:192 projection-depth:300

Build the main.c with:
    make clean; make;    

Copy paste the generated apov file and the EBOOT in an apov folder in your memory
stick. Create a file named options.txt in this folder to set the options:
MPDEPTH:0.0 HPOV:90 VPOV:1 RAYSTEP:4 WBCOUNT:1 DBCOUNT:1


### Pspgu CLUT version
For the clut version, build the main-clut with:
    make -f Makefile-Clut clean; make -f Makefile-Clut;

You need to generate the apov file as the following example:
    ./bin/apov space-block-size:256 vertical-pov-count:1 horizontal-pov-count:90 \
        ray-step:4 max-ray-depth:192 projection-depth:300 export-clut \
        compress-clut clut-compression-mode:ycbcr

Copy past the generates indexes and clut files with the EBOOT in an apov folder
in your memory stick. Then set the options as the following:
HPOV:90 VPOV:1 RAYSTEP:4 WBCOUNT:1 DBCOUNT:1


### Pspgu 1BCM version
For the 1bcm version, build the main-1bcm with:
    make -f Makefile-1bcm clean; make -f Makefile-1bcm;
    
You nee to generate the apov file as the following example:
    ./bin/apov space-block-size:256 vertical-pov-count:45 horizontal-pov-count:45 \
        ray-step:256 max-ray-depth:128 projection-depth:400 use-1bit-color-mapping \
        export-header color-map-size:8

This options file is not needed.

What is 1BCM?
1BCM means "1 bit color mapping". The idea is to generate two frames which could
be mapped for drawing the current point of view. In the first frame a single bit
is used to represent each individual visible voxel. The second frame is a RGB(+D)
rendering of the visible colored voxels in low definition.

See the atomic-point-of-view for more information.