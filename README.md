# apov-gu-navigator
```
See atomic-point-of-view project to generate the space.

For this gu version, space-block-size should be 256. Example:
    ./bin/apov space-block-size:256 vertical-pov-count:1 horizontal-pov-count:90 \
        ray-step:4 max-ray-depth:192 projection-depth:300

Build the main.c with:
    make clean; make;    

Copy paste the generated apov file and the EBOOT in an apov folder in your memory
stick. Create a file named options.txt in this folder to set the options:
MPDEPTH:0.0 HPOV:90 VPOV:1 RAYSTEP:4 WBCOUNT:1 DBCOUNT:1

For the clut version, build the main-clut with:
    make -f Makefile-Clut clean; make -f Makefile-Clut;

You need to generate the apov file as the following example:
    ./bin/apov space-block-size:256 vertical-pov-count:1 horizontal-pov-count:90 \
        ray-step:4 max-ray-depth:192 projection-depth:300 export-clut \
        compress-clut clut-compression-mode:ycbcr

Copy past the generates indexes and clut files with the EBOOT in an apov folder
in your memory stick. Then set the options as the following:
HPOV:90 VPOV:1 RAYSTEP:4 WBCOUNT:1 DBCOUNT:1
