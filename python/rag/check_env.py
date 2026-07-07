import sys

import jieba
import numpy as np
import sklearn
import zmq

def main() -> None:
    print("Python environment check")
    print(f"Python: {sys.version.split()[0]}")
    print(f"Numpy: {np.__version__}")
    print(f"scikit-learn: {sklearn.__version__}")
    print(f"pyzmq: {zmq.__version__}")
    
    tokens = list(jieba.cut("空调怎么打开"))
    print(f"jieba tokens: {tokens}")
    
    print("Environment OK")
    
if __name__ == "__main__":
    main()
    