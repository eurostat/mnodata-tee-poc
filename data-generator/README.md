
# Installing

The program requires GeoPandas for reading GeoJSON files. Install dependencies
using (we recommend setting up virtualenv before this):

```
$ pip install -r requirements.txt
```

# Running

Activate virtualenv if you use it:

```
$ source venv/bin/activate
```

Run `./generator.py --help` to read information about its
parameters. The program reads user identifiers from the standard input
with one identifier per line. For example, to generate data for 5
users and 2 days we can use:

```
$ seq 1 10 | ./generator.py --users 5 --start 2021-01-01 --end 2021-01-02
```

You can also use the `--builtin-ids` argument if integer identifiers are OK:

```
$ ./generator.py --builtin-ids --users 5 --start 2021-01-01 --end 2021-01-02
```

The intra-period footprint (H) files will be in the output directory.

If you use the duplication logic be sure to provide enough
identifiers. If there are n users and m duplicates there should be n *
(m + 1) identifiers.
