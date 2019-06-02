# djvudict

*djvudict* is an ad-hoc project for *minidjvu*. I mean you shall copy its sources to minidjvu sourcecode, apply a few patches and build minidjvu as usual. After that *minidjvu* will compile a one more executable named *djvudict*.

# Step by step:
1. Get *minidjvu* sources (you may also check if it's buildable in your system)
2. Copy content of `djvudict/tools` folder to tools subfolder of *minidjvu*
3. Apply `Makefile.am.diff` patch to enable their compilation
4. Apply `jb2coder.h.diff` patch to let djvu code access to one of private functions
5. Build *minidjvu* as usual

# Example
(I'm getting debianized minidjvu sources from github)
```
# get to temp dir (you may use another)
cd /tmp
# get minidjvu and djvudict sources
git clone https://github.com/barak/minidjvu.git
git clone https://github.com/trufanov-nok/djvudict

cd ./minidjvu

# copy djvudict sourcecode
cp ../djvudict/tools/* ./tools/

# apply patches
git apply ../djvudict/Makefile.am.diff
git apply ../djvudict/

# build minidjvu as described in its README
autoreconf --install
./configure
make
```

You shall find `djvudict` binary in the same folder as `minidjvu`
