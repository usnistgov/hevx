#!/usr/bin/env python3
from argparse import ArgumentParser
import os
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET

PAGE_HEADER = """<!DOCTYPE html>
<html>
  <head>
    <title>HEVx Test Results</title>
    <style>
      .summary {{ font-weight: bold }}
      .passed {{ color: green }}
      .failed {{ color: red }}
    </style>
  </head>
  <body>
    <h1>Summary</h1>
    <table class="summary">
      <tr><td>Time</td><td>{date_time}</td></tr>
      <tr class="passed"><td>Passed</td><td>{num_passed}</td></tr>
      <tr class="failed"><td>Failed</td><td>{num_failed}</td></tr>
    </table>"""

PAGE_FOOTER = """  </body>
</html>"""

TABLE_HEADER = """    <h1>{title}</h1>
    <table>
      <tr><th>Name</th><th>Execution Time (s)</th><th>Completion Status</th></tr>"""

TABLE_ITEM = \
        """     <tr><td>{name}</td><td>{execution_time}</td><td>{completion_status}</td></tr>"""

TABLE_FOOTER = """    </table>"""

INDEX_PAGE = """<!DOCTYPE html>
<html>
  <head>
    <title>HEVx Continuous Integration</title>
    <style>
      .alignedleft { text-align: left }
      .passed { color: green; text-align: center; }
      .failed { color: red; text-align: center; }
    </style>
  </head>
  <body>
    <h1>Summary Results</h1>
    <table>
      <tr><th class="alignedleft">Date</th><th>Passed</th><th>Failed</th><th>Details</th></tr>
      <!-- !! DO NOT REMOVE THIS LINE !! -->
    </table>
  </body>
</html>"""

INDEX_ITEM = """      <tr><td class="alignedleft">{}</td><td class="passed">{}</td><td class="failed">{}</td><td><a href="{}">Details</a></td></tr>"""

def log_msg(msg, is_error=False):
    global args
    if is_error or not args.silent:
        print("create-report: {}".format(msg))

def parse_test(test):
    values = dict()
    values['name'] = test.find('./Name').text
    values['execution_time'] = test.find('./Results/NamedMeasurement[@name="Execution Time"]/Value').text
    values['completion_status'] = test.find('./Results/NamedMeasurement[@name="Completion Status"]/Value').text
    return values

def write_report(html_dir, html_fn, test_root):
    passed_tests = test_root.findall('./Testing/Test[@Status="passed"]')
    failed_tests = test_root.findall('./Testing/Test[@Status="failed"]')

    values = dict()
    values['date_time'] = test_root.find('./Testing/StartDateTime').text
    values['num_passed'] = len(passed_tests)
    values['num_failed'] = len(failed_tests)

    html_path = os.path.join(html_dir, html_fn)
    log_msg("writing report to {}".format(html_path))
    with open(html_path, 'w') as fh:
        fh.write(PAGE_HEADER.format(**values))

        fh.write(TABLE_HEADER.format(title="Passed Tests"))
        for test in passed_tests:
            fh.write(TABLE_ITEM.format(**parse_test(test)))
        fh.write(TABLE_FOOTER)

        fh.write(TABLE_HEADER.format(title="Failed Tests"))
        for test in failed_tests:
            fh.write(TABLE_ITEM.format(**parse_test(test)))
        fh.write(TABLE_FOOTER)

        fh.write(PAGE_FOOTER)

    return (values['num_passed'], values['num_failed'])

def update_index(index_path, date_time, num_passed, num_failed, html_fn):
    log_msg("updating index at {}".format(index_path))
    with open(index_path, 'r') as fh_in:
        index_html = fh_in.readlines()
        out_fn = None

        with tempfile.NamedTemporaryFile('w', delete=False) as fh_out:
            out_fn = fh_out.name
            for line in index_html:
                if re.search('<!-- !! DO NOT REMOVE THIS LINE !! -->', line):
                    fh_out.write(INDEX_ITEM.format(date_time, num_passed, num_failed, html_fn))
                    fh_out.write(line)
                else:
                    fh_out.write(line)

    if out_fn is not None:
        shutil.copystat(index_path, out_fn)
        shutil.move(out_fn, index_path)

def create_report():
    global args

    base_dir = os.path.join(args.test_dir, args.build_config, 'Testing')
    if not os.path.isdir(base_dir):
        log_msg("{} is not an existing directory; exiting".format(base_dir), True)
        sys.exit(1)

    exclude_dirs = ['TAG', 'Temporary']
    out_dirs = [d for d in os.scandir(base_dir) if d.name not in exclude_dirs]
    out_dirs.sort(key=lambda d: d.stat().st_atime, reverse=True)

    test_dir = os.path.join(base_dir, out_dirs[0]);
    log_msg("reading CTest output from {}".format(test_dir))

    test_root = ET.parse(os.path.join(test_dir, 'Test.xml')).getroot()
    date_time = test_root.find('./Testing/StartDateTime').text

    html_fn = '_'.join(date_time.split(' ')) + '.html'
    num_passed, num_failed = write_report(args.html_dir, html_fn, test_root)

    index_path = os.path.join(args.html_dir, 'index.html')
    if not os.path.exists(index_path):
        log_msg("creating index at {}".format(index_path))
        with open(index_path, 'w') as fh:
            fh.write(INDEX_PAGE)
    update_index(index_path, date_time, num_passed, num_failed, html_fn)

def parse_args():
    global args

    parser = ArgumentParser(description="Create HTML report from CTest XML output.")
    parser.add_argument('-s', '--silent', action='store_true',
            help="Silent or quiet mode. Don't show messages.")
    parser.add_argument('-O', dest='html_dir',
            help="Root of HTML output.")
    parser.add_argument('-C', dest='build_config',
            help="CMake build configuration.")
    parser.add_argument('test_dir', help="Root of CTest output.")

    args = parser.parse_args()

if __name__ == "__main__":
    parse_args()
    create_report()
