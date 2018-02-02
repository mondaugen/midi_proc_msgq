import domelang

eps=1.0e-6

def test_result_default(observed , desired):
    if abs(observed - desired) < eps:
        print("passed")
    else:
        print("failed, observed: %f, desired: %f" % (observed,desired))

parser=domelang.Dome()

domelang.DEBUG=True

tests = [
        (
            '123.456 789 + 2.1 +',
            123.456 + 789 + 2.1,
            [],
            test_result_default
        ),
        (
            '123.456 789 + 2.1 ×',
            (123.456 + 789 ) * 2.1,
            [],
            test_result_default
        ),
        (
            '1.23456e2 789 + 2.1 × 3.5 ÷',
            ((123.456 + 789 ) * 2.1) / 3.5,
            [],
            test_result_default
        ),
        (
            '1.23456e2 789 + 2.1 × 3.5 /',
            ((123.456 + 789 ) * 2.1) / 3.5,
            [],
            lambda o,d: print("passed")
        )
]

for c,a,s,t in tests:
    parser.parse(s,c)
    t(s[-1],a)
