# djvudict
A Minidjvu-based tool to dump Djbz and Sjbz content of DjVu documents to BMP images as well as gather statistics of dictionaries usage. It allows to investigate and compare the quality of different DjVu encoders (in scope of JB2 images).

# DjVu dictionaries in a nutshell
There are several DjVu encoders that allows to encode DjVu document from images. Depending on their settings they produce djvu files of different size. One of the reason for that is that encoders organizes dictionaries in file in a sightly different way.
DjVu file may contain several layers (chunks). One of which is usually a black and white image in JB2 format that encodes text data. This chunk has id "Sjbz". JB2 image format is basically a sequence of instructions. Most of these instructions contain a small binary image (a letter). This letter can be drawn on image at specified position and/or remembered for future usage. So next time you may just tell system a number of already decoded letter to draw it. I call this local dictionary and it allows to achieve a huge compression rate.

Each page in multi-page DjVu has its own JB2 chunk and so each chunk has own local dictionary. But additionally it may contain a so called "shared dictionary". This allows to use same letter bitmap across multiple pages (usually encoders do one shared dictionary for each 10-20 pages). Shared dictionary is stored in a chunk with id "Djbz" and actually is a JB2 image too, but this image has size 0,0 and all its instructions just ask to remember letter and never ask to draw it. So basically shared dictionary is a local dictionary of a virtual empty image. When page in multi-page DjVu is decoded it may request a shared dictionary and copy N images from it to it's own local dictionary before any JB2 instructions is processed.

JB2 format also supports so called refinement encoding of single letter instead of direct encoding (just compressed encoding of whole bitmap). In case of refinement encoding it chooses one of already known letters as prototype and encode just differences of this letter in comparison to letter you want to encode. This allows additional compression for example for letters which are different, but very similar (like O and 0 (zero)). Letter may be refinement encoded based on prototype from local or shared dictionary and be drawn and/or added to dictionary too. Letter may be refinement encoded based on prototype which be is refinement encoded it's turn.

That's in a nutshell how dictionaries works (technical implementation may differ for performance reasons).

# DjVu encoders and dictionaries in a nutshell

Each DjVu encoder basically do following for one or several images it needs to encode:
1. Detect the text area
2. Split it on single letters
3. Classify letters in terms of similarity. All letters of single class will be encoded by only one bitmap ("representative")
4. Decide which letters may go to a shared dictionary, which to a local dictionary (not used on other pages) and which are unique for page and should be just encoded in it.
5. Decide which "representatives" in dictionaries or pages may be additionally compressed by choosing a prototype and refinement encoding.

Especially important is step 3. On step 5 you can't damage the resulting image in comparison to it's original bcs it's all just about effective bitmap compression without changing it. But on step 3 you choose one single bitmap that will replace all occurrences of letters of the same class. (for ex., minidjvu chooses 1st bitmap among bitmaps classified to single class or tries to draw new averaged bitmap based on them if `--Averaging` param is specified).

As you can see that's a very complex problem and different encoders solve it differently. They also may have different settings that affect the result in a different way.

# What is this tool for?

`djvudict` is just a tool that allows to look on how dictionaries is organized in already encoded DjVu document. It supports bundled single-page and multi-page documents.

The usage is:  
`djvudict <djvu_file> <folder_to_output>`

For each page and shared dictionary in DjVu document it creates a folder with name <id>_<pagename>.
Then for each JB2 image in document (Djbz and Sjbz) to will save all bitmaps that were added to their local dictionaries or just drawn as BMP images. For example it will save all bitmaps added to shared dictionary (Djbz) as its a 0-sized JB2 image too. And for each page of document (Sjbz) it will save all bitmaps that were added to it's local library (excluding shared dictionary images) or just directly drawn on image.
It also creates actions.log file in each subfolder that contains a list of JB2 instructions with dictionaries indexes as they appeared in JB2 image.
Finally it creates a stats.log in each subfolder and folder. These files contain some statistical data on JB2 instruction usage per page and totally as well as number of access to shared oк local dictionaries and number of elements on page/pages.
  
# Building from sources

Check INSTALL file for details.
