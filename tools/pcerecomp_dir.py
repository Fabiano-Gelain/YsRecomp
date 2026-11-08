"""Onde está o PCERecomp (o console emulado e as ferramentas genéricas), para os scripts do YsRecomp.

Ordem: a variável PCERECOMP_DIR; o submódulo external/PCERecomp; a pasta ao lado (../PCERecomp).
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find():
    env = os.environ.get("PCERECOMP_DIR")
    if env:
        return os.path.abspath(env)
    sub = os.path.join(ROOT, "external", "PCERecomp")
    if os.path.isfile(os.path.join(sub, "CMakeLists.txt")):
        return sub
    return os.path.abspath(os.path.join(ROOT, "..", "PCERecomp"))


PCERECOMP = find()
TRACE_TOOLS = os.path.join(PCERECOMP, "tools", "trace")
GG_TRACE = os.path.join(PCERECOMP, "tools", "reference_trace", "build", "Release", "gg_trace.exe")
