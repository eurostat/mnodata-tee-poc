
# Installing

The fingerprint_report_heatmap.py script depends on matplotlib and seaborn.
Dependencies can be installed using (we recommend to set up virtualenv before
this):

```
$ pip install -r requirements.txt
```


# Running

Activate virtualenv if you use it:

```
$ source venv/bin/activate
```

Run the data generator, analysis and heatmap scripts:

```
$ ./circle_generator.py --builtin-ids
$ ./analysis.py
$ ./fingerprint_report_heatmap.py
```

Generators write CSV files into the `data` directory and `analysis.py` writes
CSV reports into the `reports` directory.

# Code documentation

`parameters.py` contains parameters such as the number of days and number of
users.

`simple_generator.py` generates footprint updates data as CSV files
(in the `data` directory). This generator is unrealistic and is only
intended for testing the rest of the code. Use `--builtin-ids`
argument for testing the prototype without pseudonyms.

`circle_generator.py` generates footprint updates so that people spend
more time in a circle in the center of a rectangular "country". This
data is for checking that the produced fingerprint report is as
expected. Use `--builtin-ids` argument for testing the prototype
without pseudonyms.

`analysis.py` implements the footprint accumulation and report calculation
process.

`fingerprint_report_heatmap.py` plots the fingerprint report that was written by
`analysis.py`.
