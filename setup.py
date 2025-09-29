from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension

ext_modules = [
    Pybind11Extension(
        "poker_env",  # 模块名
        ["poker_env.cpp"],  # 源文件
    ),
]
setup(
    name="poker_env",
    ext_modules=ext_modules,
)
