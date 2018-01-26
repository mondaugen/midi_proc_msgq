# bunk dank dome

import re

# Mode when waiting for operation
# for example, if we are in this mode and we encounter =, we expect a character
# or a string. If either of those is encountered, we start a function definition
DOME_MODE_DISPATCH=1
DOME_MODE_FUNDEF=2
DOME_MODE_INTERPRET=3


def _plus(s,a):
    x=s.pop()
    y=s.pop()
    s.append(x+y)

# Note these are stored in the order that they are matched against. The first
# one to match is executed.
cmd_parsers=[
        # Command name, pattern matcher, auxilary preparation function, function
        (
            'FLOAT',
            '[-+]?\d+\.\d*([eE][-+]?\d+|)',
            lambda x: float(x),
            lambda s,a: s.append(a)
        ),
        (
            'INT',
            '[-+]?\d+',
            lambda x: int(x),
            lambda s,a: s.append(a)
        ),
        (
            'PLUS',
            '\+',
            None,
            lambda s,a: _plus
        ),
        (
            'NOP',
            ' ',
            None,
            None
        )
]


class Dome:
    def __init__(self):
        self.mode = DOME_MODE_DISPATCH
        self.cmd_parsers=[]
        for n,p,a,f in cmd_parsers:
            self.cmd_parsers.append((n,re.compile(p),a,f))
    def parse(self,stack,cmds):
        """
        Parse the cmnds, effecting them on the stack.
        """
        while cmds:
            for _,p,_,_ in self.cmd_parsers:
                m=p.match(cmds)
                if m:




