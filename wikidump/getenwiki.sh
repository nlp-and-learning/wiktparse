#!/bin/bash
read -r DATE < DATE
wget -c https://mirror.accum.se/mirror/wikimedia.org/dumps/enwiki/20250620/enwiki-$DATE-pages-articles-multistream-index.txt.bz2
wget -c https://mirror.accum.se/mirror/wikimedia.org/dumps/enwiki/20250620/enwiki-$DATE-pages-articles-multistream.xml.bz2
