import capnp
import api.Simulator_capnp as api
import matplotlib.pyplot as plt
from cmath import phase
import numpy as np

ckt = """* test
V1 1 0 AC 1 sin(0 5 1k)
R1 1 2 1k
C2 2 0 1u
.end
"""
print(ckt)

sim = capnp.TwoPartyClient('localhost:5923').bootstrap().cast_as(api.Ngspice)
res = sim.loadFiles([{"name": "bar.sp", "contents": ckt}]).wait()

raw_vectors = res.commands.tran(1e-6, 1e-3, 0).result.readAll().wait()
print(raw_vectors)
vectors = {}
for vec in raw_vectors.data:
    vectors[vec.name] = vec.data

print(vectors.keys())

def map_complex(vec):
    return np.array([complex(v.real, v.imag) for v in vec.complex])

if "time" in vectors:
    plt.figure(1)
    t = vectors['time'].real
    plt.plot(t, vectors['V(1)'].real)
    plt.plot(t, vectors['V(2)'].real)
elif "freq" in vectors:
    plt.figure(2)
    f = np.real(map_complex(vectors['freq']))
    v2 = map_complex(vectors['V(2)'])

    plt.subplot(211)
    plt.loglog(f, np.abs(v2))
    plt.subplot(212)
    plt.semilogx(f, np.rad2deg(np.angle(v2)))
plt.show()