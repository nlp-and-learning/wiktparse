#!/bin/bash
read -r DATE < DATE
wget -c https://mirror.accum.se/mirror/wikimedia.org/dumps/$1/$DATE/$1-$DATE-pages-articles-multistream.xml.bz2
wget -c https://mirror.accum.se/mirror/wikimedia.org/dumps/$1/$DATE/$1-$DATE-pages-articles-multistream-index.txt.bz2
