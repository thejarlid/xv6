import os
from subprocess import call
from multiprocessing import Process
import time
from subprocess import Popen, PIPE

def main():
    garbage = open("garbage.txt", 'w');
    call(["make","clean"], stdout = garbage, stderr = garbage)
    if os.path.exists("input.txt"):
        os.remove("input.txt")

    if os.path.exists("output.txt"):
        os.remove("output.txt")

    call(["make"], stdout = garbage, stderr = garbage)

    w = open("output.txt", 'w')

    process = Popen([r'make', 'qemu'], stdin=PIPE, stdout=w)
    i = 0
    while process.poll() == None:
        process.stdin.write("lab5test_b\n")
        time.sleep(0.2)
        i += 1
        if i > 30:
            break

    process.terminate()
    call(["pkill","qemu"], stdout = garbage, stderr = garbage)
    garbage.close()
    w.close()
    os.remove("garbage.txt")

    r = open("output.txt", 'r')
    buf = r.read()
    if "consistent" in buf:
        print "file system is not crash-safe"
    elif "lab5test_b passed!" in buf:
        print "file system is crash-safe"
    else:
        print "test is not finished yet!"

    r.close()

if __name__ == "__main__":
    main()
