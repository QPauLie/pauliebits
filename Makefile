PYTHON=python


pauliebits/_pauliebits.so: pauliebits/_pauliebits.c
	$(PYTHON) setup.py build_ext --inplace


test: pauliebits/_pauliebits.so
	$(PYTHON) setup.py test


install:
	$(PYTHON) -m pip install -vv .


doc: pauliebits/_pauliebits.so
	$(PYTHON) update_doc.py
	$(PYTHON) setup.py sdist
	twine check dist/*


mypy:
	mypy pauliebits/*.pyi
	mypy pauliebits/test_*.py
	mypy examples/*.py
	mypy examples/huffman/*.py
	mypy examples/sparse/*.py


clean:
	rm -rf build dist
	rm -f pauliebits/*.o pauliebits/*.so
	rm -f pauliebits/*.pyc
	rm -f examples/*.pyc
	rm -rf pauliebits/__pycache__ *.egg-info
	rm -rf examples/__pycache__ examples/*/__pycache__
	rm -rf .mypy_cache pauliebits/.mypy_cache
	rm -rf examples/.mypy_cache examples/*/.mypy_cache
