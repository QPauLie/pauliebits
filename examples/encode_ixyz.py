from pauliebits import pauliebits
from random import choice
from time import perf_counter
from itertools import combinations


def get_random(n_qubits: int) -> str:
    """ 
    Get random Pauli String for n_qubits
    Args:
         n_qubits (int): Number of qubits
    Returns:
         str
         Random Pauli string

    """
    return''.join([choice("IXYZ") for _ in range(n_qubits)])

def get_random_list(n_qubits:int, length: int) -> list[str]:
    """ 
    Get random list of Pauli strings of length `length` for `n_qubits`
    Args:
         n_qubits (int): Number of qubits
         length (int): List size
    Returns:
         list[str]
         A list of random Pauli string

    """
    return [get_random(n_qubits) for _ in range(length)]


CODEC = {
    "I": pauliebits([0, 0]),
    "X": pauliebits([1, 0]),
    "Y": pauliebits([1, 1]),
    "Z": pauliebits([0, 1]),
}

def create(paulistring):
    ba = pauliebits()
    ba.encode(CODEC, paulistring)
    return ba

def create_ixyz(paulistring):
    ba = pauliebits()
    ba.encode_ixyz(paulistring)
    return ba

paulistrings = get_random_list(10000, 1000)


start_time = perf_counter()
[create(paulistring) for paulistring in paulistrings]
end_time = perf_counter()
print(f"encode: {end_time - start_time}")

start_time = perf_counter()
g = [create_ixyz(paulistring) for paulistring in paulistrings]
end_time = perf_counter()
print(f"encode_ixyz: {end_time - start_time}")

start_time = perf_counter()
for s1, s2 in combinations(g, r=2):
    r = s1.commutes_with(s2)
end_time = perf_counter()
print(f"commutes_with: {end_time - start_time}")



