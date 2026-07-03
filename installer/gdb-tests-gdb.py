"""GDB Python customization auto-loader for GDB test executable"""

import os.path
sys.path[0:0] = [os.path.join('C:/spidermonkey-dev/gecko-dev/js/src', 'gdb')]

import mozilla.autoload
mozilla.autoload.register(gdb.current_objfile())

import mozilla.asmjs
mozilla.asmjs.install()
