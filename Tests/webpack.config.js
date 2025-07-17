const path = require('path');

module.exports = {
  entry: './UnitTests/Scripts/tests.ts',
  output: {
    path: path.resolve(__dirname, 'UnitTests/dist'),
    filename: 'tests.js',
    library: {
      type: 'var',
      name: 'Tests'
    },
    globalObject: 'this'
  },
  target: ['web', 'es5'],
  mode: 'production',
  resolve: {
    extensions: ['.ts', '.js'],
    fallback: {
        // ChakraCore compatibility - disable all Node.js modules
        'fs': false,
        'path': false,
        'util': false,
        'process': false,
        'buffer': false,
        'stream': false,
        'events': false,
        'assert': false,
        'crypto': false,
        'url': false,
        'querystring': false,
        'http': false,
        'https': false,
        'zlib': false,
        'tty': false,
        'os': false,
        'child_process': false,
        'net': false,
        'dgram': false,
        'dns': false,
        'readline': false,
        'repl': false,
        'cluster': false,
        'vm': false,
        'domain': false,
        'constants': false,
        'module': false,
        'timers': false
    }
},
  module: {
    rules: [
      {
        test: /\.ts$/,
        use: 'ts-loader',
        exclude: /node_modules/
      }
    ]
  },
  externals: {
    'node:path': 'commonjs node:path',
    'crypto': 'commonjs crypto',
    'events': 'commonjs events',
    'node:fs': 'commonjs node:fs',
    'debug': 'commonjs debug',
    'node:util': 'commonjs node:util',
    'node:events': 'commonjs node:events',
    'node:url': 'commonjs node:url',
    'node:process': 'commonjs node:process',
    'workerpool': 'commonjs workerpool',
    'he': 'commonjs he',
    'loupe': 'commonjs loupe'
  },
    optimization: {
        minimize: false // Keep code readable for debugging
    },
    devtool: 'source-map'
};


