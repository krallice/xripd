def FlagsForFile( filename, **kwargs ):
  return {
      'flags': [ '-x', 'c', '-g', '-std=gnu99', '-Wall', ]
  }
