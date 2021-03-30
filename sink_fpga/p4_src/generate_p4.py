from jinja2 import Template
import shutil
import sys

int_meta_file = open("./temps/int_meta_hdr.txt", "r")
meta_tm = Template(int_meta_file.read())
meta_msg = meta_tm.render(n=int(sys.argv[1]))

parser_file = open("./temps/parser.txt", "r")
parser_tm = Template(parser_file.read())
parser_file = open("./parser.p4", "w")
parser_file.write(parser_tm.render(meta=meta_msg))

shutil.copy("./temps/header.txt", "./header.p4")
shutil.copy("./temps/top.txt", "./top.p4")

