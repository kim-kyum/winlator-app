# Prints the centre "x y" of the first node in a uiautomator dump whose attribute matches.
# Usage: find.py <dump.xml> <attribute> <value>   (a value starting with ~ matches a part)
import re
import sys
import xml.etree.ElementTree as ET

dump, attr, value = sys.argv[1:4]
try:
    root = ET.parse(dump).getroot()
except Exception:
    sys.exit(1)
for node in root.iter("node"):
    v = node.get(attr, "")
    if v == value or (value.startswith("~") and value[1:] in v):
        b = [int(x) for x in re.findall(r"\d+", node.get("bounds", ""))]
        if len(b) == 4 and b[2] > b[0] and b[3] > b[1]:
            print((b[0] + b[2]) // 2, (b[1] + b[3]) // 2)
            sys.exit(0)
sys.exit(1)
