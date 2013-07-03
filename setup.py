#!/usr/bin/env python
#

import sys
import os
from setuptools import setup, Extension

def get_description():
    README = os.path.abspath(os.path.join(os.path.dirname(__file__), 'README'))
    f = open(README, 'r')
    try:
        return f.read()
    finally:
        f.close()

VERSION = "0.1.3"

if sys.platform.startswith("cygwin"):

    def get_winver():
        return '0x0503'
        maj, min = sys.getwindowsversion()[0:2]
        return '0x0%s' % ((maj * 100) + min)

    extensions = [Extension('netuse',
                            sources=['src/netuse.c'],
                            define_macros=[('_WIN32_WINNT', '0x0503'),
                                           ('USE_SYS_TYPES_FD_SET', 1)],
                            libraries=["kernel32", "advapi32", "shell32",
                                       "netapi32", "mpr"],
                            #extra_compile_args=["/Z7"],
                            #extra_link_args=["/DEBUG"]
                            )]
                                                        
else:
    sys.exit('platform %s is not supported' % sys.platform)

def main():
    setup_args = dict(
        name='netdrive',
        version=VERSION,
        description='A tool used to report the usage of net drive in the Windows',
        long_description=get_description(),
        keywords=['netdrive',],
        py_modules=['netreport'],
        author='Nexedi',
        author_email='jondy.zhao@nexedi.com',
        maintainer='Jondy Zhao',
        maintainer_email='jondy.zhao@nexedi.com',
        license='GPLv3',
        zip_safe=False,
        install_requires=[
            'lxml', 
            'slapos.core',
            'setuptools',
            'zc.buildout', # plays with buildout
            'zc.recipe.egg', # for scripts generation
            ],
        entry_points={
            'console_scripts': [
                'netdrive-reporter = netreport:main',
                ],
            },
        )
    if extensions is not None:
        setup_args["ext_modules"] = extensions
    setup(**setup_args)

if __name__ == '__main__':
    main()
