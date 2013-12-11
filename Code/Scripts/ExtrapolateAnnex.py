#! /usr/bin/env python

"""
Extrapolate a bunch of waveforms.

Note that this uses a sqlite3 database for batch runs.  If this
feature is used, it will need to be able to lock the db file, so that
multiple extrapolations don't try to run on the same data.  This means
that the db file can't be stored on NFS (because NFS is broken when it
comes to file locking).  Usually, there is some accessible filesystem
that is not NFS, so the file must simply be stored there.

"""


Template = r"""#! /usr/bin/env python
## Automatically generated by {ScriptName}

# Set up the paths
D = {}
D['DataFile'] = '{DataFile}'

# Find ChMass from metadata.txt
import re
ChMass = 0.0
try :
    with open('metadata.txt', 'r') as file :
        for line in file :
            m = re.match(r'\s*relaxed-mass[12]\s*=\s*([0-9.]*)', line)
            if(m) : ChMass += float(m.group(1))
    D['ChMass'] = ChMass
except :
    print("\nWARNING: Could not find metadata.txt in '{Directory}'")

# Now run the actual extrapolation
import GWFrames.Extrapolation
try :
    GWFrames.Extrapolation.Extrapolate(**D)
except Exception as e : # Pass exceptions to shell as failures
    from sys import exit
    print e
    exit(1)
"""





def SetUpDB(start_rev, db) :
    """Set up the database file of extrapolations to be run."""
    import os.path
    
    # Update the repository (and python error on git error)
    subprocess.check_call('git pull --rebase', shell=True)
    subprocess.check_call('git annex merge', shell=True)
    
    # Get the list of changed files since the last git revision
    ChangedFiles = subprocess.check_output('set -o pipefail; git diff --name-only {0}..HEAD | (grep _FiniteRadii_CodeUnits.h5 || true)'.format(start_rev), shell=True)
    
    # If nothing changed, end here
    if(ChangedFiles=='') :
        print("No changed files; nothing to extrapolate.")
        sys.exit(0)
    else :
        ChangedFiles = ChangedFiles.split()
    
    # Get the changed data files
    for ChangedFile in ChangedFiles :
        print('Getting data:  git annex get {0} and Horizons.h5'.format(ChangedFile))
        subprocess.check_output('git annex get {0}'.format(ChangedFile), shell=True)
        subprocess.check_output('git annex get {0}'.format(os.path.dirname(ChangedFile)+'/Horizons.h5'), shell=True)
    
    # Write the database file
    conn = sqlite3.connect(db, timeout=60)
    try :
        conn.isolation_level = 'EXCLUSIVE'
        conn.execute('BEGIN EXCLUSIVE')
    except sqlite3.OperationalError as e :
        import textwrap
        print(textwrap.dedent(
            """
            ERROR: The database could not be locked.
            
                   If you are trying to create this on an NFS file
                   system, note that NFS may be buggy with regards to
                   locking.  Try locating the database file on a
                   non-NFS partition by giving a different path to the
                   `--db` argument.  (Everything else can stay where
                   it is.)
            
            """))
        conn.close()
        raise
    c = conn.cursor()
    try :
        c.execute("""CREATE TABLE extrapolations (datafile text, status integer, UNIQUE(datafile) ON CONFLICT REPLACE)""")
    except sqlite3.OperationalError :
        print("Database exists already.  Removing records of failure when data is new.")
    for ChangedFile in ChangedFiles :
        c.execute("""INSERT INTO extrapolations VALUES ('{0}',0)""".format(ChangedFile))
    conn.commit()
    conn.close()

def RunExtrapolation(DataFile, Template) :
    import os
    import os.path
    import subprocess
    from GWFrames.Extrapolation import _safe_format
    
    Directory,FileName = os.path.split(DataFile)
    
    # Copy the template file to Directory
    with open('{0}/Extrapolate_{1}.py'.format(Directory,FileName[:-3]), 'w') as TemplateFile :
        TemplateFile.write(_safe_format(Template, DataFile=FileName, Directory=Directory))
    
    # Try to run the extrapolation
    OriginalDir = os.getcwd()
    try :
        try :
            os.chdir(Directory)
        except :
            print("Couldn't change directory to '{0}'.".format(Directory))
            raise
        print("\n\nRunning {1}/Extrapolate_{0}.py\n\n".format(FileName[:-3], os.getcwd()))
        ReturnValue = subprocess.call("set -o pipefail; python Extrapolate_{0}.py 2>&1 | tee Extrapolate_{0}.log".format(FileName[:-3]), shell=True)
        if(ReturnValue) :
            print("\n\nExtrapolateAnnex got an error ({2}) on '{0}/{1}'.\n\n".format(Directory, FileName, ReturnValue))
            os.chdir(OriginalDir)
            return ReturnValue
        print("\n\nFinished Extrapolate_{0}.py in {1}\n\n".format(FileName[:-3], os.getcwd()))
    except :
        print("\n\nExtrapolateAnnex got an error on '{0}/{1}'.\n\n".format(Directory, FileName))
    finally :
        os.chdir(OriginalDir)
    return 0



if __name__ == "__main__" :
    import subprocess
    import sys
    import argparse
    import sqlite3
    
    # Set up and run the parser
    parser = argparse.ArgumentParser(description = __doc__)
    parser.add_argument('--run', action='store_true',
                        help="run the first non-errored extrapolation in the database")
    parser.add_argument('--db', default='ExtrapolationsToRun.db',
                        help='create the named sqlite3 database to track which extrapolations need to be done')
    parser.add_argument('--since_my_last_commit', action='store_true',
                        help="only run data newer than the user's last commit")
    parser.add_argument('--start_rev', default='',
                        help='hash of the git revision at which to start counting changes')
    args = vars(parser.parse_args(sys.argv[1:]))
    
    # If wanted, just run the first extrapolation in the database
    if(args['run']) :
        while(True) :  # Loop until the program is killed, or we break out below
            # Note that this run has started
            conn = sqlite3.connect(args['db'], timeout=60)
            conn.isolation_level = 'EXCLUSIVE'
            conn.execute('BEGIN EXCLUSIVE')
            c = conn.cursor()
            try :
                datafile = [r for r in c.execute("""SELECT datafile FROM extrapolations WHERE status=0 LIMIT 1""")][0][0]
            except IndexError :
                # No such datafiles found; we are finished!
                conn.commit()
                conn.close()
                sys.exit(0)
            c.execute("""UPDATE extrapolations SET status=1 WHERE datafile='{0}'""".format(datafile))
            conn.commit()
            conn.close()
            
            # Run the extrapolation
            print(datafile)
            ReturnValue = RunExtrapolation(datafile, Template)
            print("ReturnValue = {0}".format(ReturnValue))
            
            # on failure, write status=-1
            if(ReturnValue) :
                conn = sqlite3.connect(args['db'], timeout=60)
                conn.isolation_level = 'EXCLUSIVE'
                conn.execute('BEGIN EXCLUSIVE')
                c = conn.cursor()
                c.execute("""UPDATE extrapolations SET status=-1 WHERE datafile='{0}'""".format(datafile))
                conn.commit()
                conn.close()
            else :
                # on success, just remove this record
                conn = sqlite3.connect(args['db'], timeout=60)
                conn.isolation_level = 'EXCLUSIVE'
                conn.execute('BEGIN EXCLUSIVE')
                c = conn.cursor()
                # c.execute("""UPDATE extrapolations SET status=1 WHERE datafile='{0}'""".format(datafile))
                c.execute("""DELETE FROM extrapolations WHERE datafile='{0}'""".format(datafile))
                conn.commit()
                conn.close()
        sys.exit(0)
    
    
    # Get the initial git revision
    if(args['since_my_last_commit']) :
        GitName = subprocess.check_output('git config user.name', shell=True)
        start_rev = subprocess.check_output('set -o pipefail; git log --format=%H --author="{0}" | tail -1'.format(GitName), shell=True)[:-1]
        if(not start_rev) :
            # No first commit by this author, so get hash of very first commit by anyone
            start_rev = subprocess.check_output('set -o pipefail; git log --format=%H | tail -1', shell=True)[:-1]
    else :
        if(args['start_rev']) :
            start_rev = args['start_rev']
        else :
            start_rev = subprocess.check_output('git rev-parse HEAD', shell=True)[:-1]
    
    # Set up the database by looking at the git changes
    SetUpDB(start_rev, args['db'])
    