from jinja2 import Template
import shutil
import sys

parser_file = open("./temps/parser.txt", "r")
parser_tm = Template(parser_file.read())
parser_file = open("./parser.p4", "w")
parser_file.write(parser_tm.render(n=int(sys.argv[1])))

shutil.copy("./temps/header.txt", "./header.p4")
shutil.copy("./temps/top.txt", "./top.p4")

