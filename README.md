# mkxp-sunshine

This is a specialized fork of [mkxp-oneshot](https://github.com/elizagamedev/mkxp-oneshot) designed for OneShot: sunshine.

# xScripts.rxdata
./rpgscript.rb scripts/ [GameDir]

# Build
1.Install required packages
Cmake,C/C++ compiler,Ruby,Boost,SDL3,pixman,SDL3_image,SDL3_ttf,SDL3_sound,OpenAL,PhysFS,sigc++-2.0
(Linux only!)GTK3,libxfconf,

2.build

In project dir make build dir.
cmake -S . -B build
cd build
make -jn 
n - count of threads for compilation

# TODOs:
* Port to SDL3,Modern ruby and other
* Extend API with usefull thinks idk...
* Fix bugs
* Upgrade linux support
* MAKE THIS WORK 
