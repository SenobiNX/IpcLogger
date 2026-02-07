import sys

text = sys.stdin.read()

print("".join((map(lambda x: x[1], filter(lambda x: x[0] % 17 != 0, enumerate(text.split()))))))
