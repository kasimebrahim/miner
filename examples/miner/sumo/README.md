# Pattern Mining SUMO

We attempt to pattern mine SUMO to see if we can extract interesting
patterns.

## Usage

### Import SUMO

First you need to import the SUMO KB to opencog. For that clone
opencog's external-tools repository in the folder of your choice

```bash
git clone https://github.com/opencog/external-tools.git
```

then convert the kif files into scheme (it's gonna take a while)

```bash
cd <EXTERNAL-TOOLS>/SUMO_importer
./sumo-opencog.sh
```

copy the generated output (replace `<MINER_REPO>` appropriately)

```bash
SCM_DIR=<MINER_REPO>/examples/miner/sumo/scm
mkdir "$SCM_DIR"
cp all-sumo-labeled-kb.scm "$SCM_DIR"
cp sumo/output/*.scm "$SCM_DIR"
cd "$SCM_DIR/.."
```

### Run Pattern Miner

```bash
guile --no-auto-compile -l mine-sumo.scm
```

which will run the pattern miner over all the files. The results will
be logged in the `opencog.log` file at the lines

```
Results from mining <FILENAME>:
```

Results for some domains have been saved in the `results` folder.

### Interpreting results

Below is an example of result found in

```
results/Geography-ms300-mi1000-mc4-mv2-nisurp.scm
```

```scheme
(EvaluationLink (stv 0.98404255 1)
   (PredicateNode "isurp")
   (ListLink
      (LambdaLink
         (VariableNode "$PM-229da880")
         (PresentLink
            (InheritanceLink
               (VariableNode "$PM-229da880")
               (ConceptNode "SaltWaterArea" (stv 0.01 1))
            )
            (InheritanceLink
               (VariableNode "$PM-229da880")
               (ConceptNode "MaritimeClaimArea" (stv 0.01 1))
            )
         )
      )
      (ConceptNode "db-438037533-1Ip13XKsMBkEBFng")
   )
)
```

The truth value `(stv 0.98404255 1)` over the `isurp` predicate
evaluation of pattern

```scheme
      (LambdaLink
         (VariableNode "$PM-229da880")
         (PresentLink
            (InheritanceLink
               (VariableNode "$PM-229da880")
               (ConceptNode "SaltWaterArea" (stv 0.01 1))
            )
            (InheritanceLink
               (VariableNode "$PM-229da880")
               (ConceptNode "MaritimeClaimArea" (stv 0.01 1))
            )
         )
```

means that such pattern has an I-Surprisingness of 0.98404255 (the 1,
representing the confidence of that value, is irrelevant in that
context). Interestingly such a pattern is rather obvious to a human
who has the appropriate background knowledge, but surprising to the
pattern miner which has not, and would not at the present time take
advantage of it during its surprisingness calculation. Subsequent
versions of the pattern miner will address this problem.
