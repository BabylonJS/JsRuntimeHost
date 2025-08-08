const path = require('path');
const webpack = require('webpack');

module.exports = {
  target: 'web',
  mode: 'development', // or 'production'
  devtool: false,
  entry: {
    UnitTests: './UnitTests/Scripts/tests.ts',
  },
  output: {
    filename: '[name].js',
    path: path.resolve(__dirname, 'UnitTests/dist'),
  },
  plugins: [
    new webpack.ProvidePlugin({
    process: 'process/browser',
    Buffer: ['buffer', 'Buffer'],
  }),
    new webpack.optimize.LimitChunkCountPlugin({
      maxChunks: 1, // ensures all code is in a single chunk
    }),
  ],
  resolve: {
    extensions: ['.ts', '.js'],
    fallback: {
        'fs': false,
        'path': false,
        'util': false,
        'process': false,
        'buffer': false,
        stream: require.resolve('stream-browserify'),
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
          test: /\.(js|ts)$/,
          exclude: (modulePath) => {
            return (
              /node_modules/.test(modulePath) &&
              !/node_modules[\\/](?:@babylonjs|mocha|chai)/.test(modulePath)
            );
          },
          use: 'babel-loader',
        },
      ],
  },
  watch: false, // enables file watcher
};
