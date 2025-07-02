#!/bin/bash
curl -s https://mirror.accum.se/mirror/wikimedia.org/dumps/afwiktionary/ \
  | grep -o '<a href="20[0-9]\{6\}/"' \
  | cut -d'"' -f2 | cut -d'/' -f1 \
  | sort | tail -n1 > DATE
curl -s https://mirror.accum.se/mirror/wikimedia.org/dumps/ | grep wiktionary | grep -oE '[a-z]+wiktionary' | sort | uniq > list.txt
echo "#!/bin/bash" > getall.sh
awk '{print "./getWithIndex.sh", $0}' list.txt >> getall.sh
chmod +x getall.sh
./getall.sh