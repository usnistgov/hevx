import ycm_core
import os.path

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
database = ycm_core.CompilationDatabase(os.path.join(SCRIPT_DIR, 'build'))

SOURCE_EXTENSIONS = ['.cpp', '.cc', '.c']

def IsHeaderFile(filename):
  extension = os.path.splitext(filename)[1]
  return extension in ['.h', '.hxx', '.hpp', '.hh']


def FindCorrespondingSourceFile(filename):
  if IsHeaderFile(filename):
    basename = os.path.splitext(filename)[0]
    for extension in SOURCE_EXTENSIONS:
      replacement_file = basename + extension
      if os.path.exists(replacement_file):
        return replacement_file
  return filename

def Settings(**kwargs):
  if kwargs['language'] == 'cfamily':
    filename = FindCorrespondingSourceFile(kwargs['filename'])

    compilation_info = database.GetCompilationInfoForFile(filename)
    if not compilation_info.compiler_flags_:
        return {}

    final_flags = list(compilation_info.compiler_flags_)

    return {
        'flags': final_flags,
        'include_paths_relative_to_dir': compilation_info.compiler_working_dir_,
        'override_filename': filename
        }
  return {}
