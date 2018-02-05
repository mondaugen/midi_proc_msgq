import domelang

eps=1.0e-6

def test_result_default(observed , desired):
    passed=True
    if type(observed) == list:
        # desired must be list too
        for o,d in zip(observed,desired):
            passed = passed and (abs(o - d) < eps)
    else:
        passed = passed and (abs(observed - desired) < eps)
    if passed:
        print("passed")
    else:
        print("failed, observed: %f, desired: %f" % (observed,desired))

parser=domelang.Dome()

domelang.DEBUG=False

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
        ),
        (
            '+',
            [1.2 + 5.6 ,3.4 + 7.8],
            [[1.2,3.4],[5.6,7.8]],
            test_result_default
        ),
        (
            '+',
            [1.2 + 5.6 ,3.4 + 5.6],
            [[1.2,3.4],5.6],
            test_result_default
        ),
        (
            '+',
            [1.2 + 5.6 ,3.4 + 5.6],
            [[1.2,3.4],[5.6]],
            test_result_default
        ),
        (
            '+',
            [1.2 + 5.6 ,3.4 + 7.8, 9.0 + 5.6],
            [[1.2,3.4,9.0],[5.6,7.8]],
            test_result_default
        ),
        (
            '+',
            [1.2 + 5.6],
            [[1.2],[5.6,7,8]],
            test_result_default
        ),
        (
            '+',
            1.2 + 5.6,
            [1.2,[5.6,7,8]],
            test_result_default
        )
]

for c,a,s,t in tests:
    parser.parse(s,c)
    t(s[-1],a)
    print(s[-1])
