const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyPlugin = require("copy-webpack-plugin");
const HtmlMinimizerPlugin = require("html-minimizer-webpack-plugin");
const { CleanWebpackPlugin } = require("clean-webpack-plugin");
const MiniCssExtractPlugin = require("mini-css-extract-plugin");
const CompressionPlugin = require("compression-webpack-plugin");
const TerserPlugin = require("terser-webpack-plugin");
const CssMinimizerPlugin = require("css-minimizer-webpack-plugin");
const ImageMinimizerPlugin = require("image-minimizer-webpack-plugin");
const webpack = require("webpack");
const path = require("path");
const BundleAnalyzerPlugin = require('webpack-bundle-analyzer').BundleAnalyzerPlugin;
const globSync = require("glob").sync;
const glob = require('glob');
const { merge } = require('webpack-merge');
const devserver = require('./webpack/webpack.dev.js');

const PurgeCSSPlugin = require('purgecss-webpack-plugin')
const whitelister = require('purgecss-whitelister');
const GrpcToolsNodeProtocPlugin = require('./webpack/GrpcToolsNodeProtocPlugin.js');
const buildRootPath = path.join(process.cwd(), '..', '..', '..');
const wifiManagerPath = glob.sync(path.join(buildRootPath, 'components/**/wifi-manager*'))[0];
const ComponentsPath = glob.sync(path.join(buildRootPath, 'components/'))[0];
const buildCRootPath = glob.sync(buildRootPath)[0];
const SPIFFSPath = glob.sync(path.join(buildRootPath, 'SPIFFS'))[0];


const PATHS = {
  src: path.join(__dirname, 'src')
}

class BuildEventsHook {
  constructor(name, fn, stage = 'afterEmit') {
    this.name = name;
    this.stage = stage;
    this.function = fn;
  }
  apply(compiler) {
    compiler.hooks[this.stage].tap(this.name, this.function);
  }
}


module.exports = (env, options) => (
  merge(
    env.WEBPACK_SERVE ? devserver : {},
    env.ANALYZE_SIZE ? {
      plugins: [new BundleAnalyzerPlugin(
        {
          analyzerMode: 'static',
          generateStatsFile: true,
          statsFilename: 'stats.json',
        }
      )]
    } : {},

    // { stats: 'verbose', },
    {
      entry:
      {
        index: './src/index.ts'
      },
      devtool: "source-map",
      module: {
        rules: [
          {
            test: /\.ejs$/,
            loader: 'ejs-loader',
            options: {
              variable: 'data',
              interpolate: '\\{\\{(.+?)\\}\\}',
              evaluate: '\\[\\[(.+?)\\]\\]'
            }
          },
          {
            test: /\.(woff(2)?|ttf|eot)(\?v=\d+\.\d+\.\d+)?$/,
            use: [
              {
                loader: 'file-loader',
                options: {
                  name: '[name].[ext]',
                  outputPath: 'fonts/'
                }
              }
            ]
          },
          // {
          //   test: /\.s[ac]ss$/i,

          //   use:  [{
          //     loader: 'style-loader', // inject CSS to page
          //   },
          //      {
          //         loader: MiniCssExtractPlugin.loader,
          //         options: {
          //           publicPath: "../",
          //         },
          //       },
          //     "css-loader",
          //     {
          //       loader: "postcss-loader",
          //       options: {
          //         postcssOptions: {
          //           plugins: [["autoprefixer"]],
          //         },
          //       },
          //     },
          //     "sass-loader",
          //   ]
          // },
          {
            test: /\.(scss)$/,
            use: [

              {
                loader: MiniCssExtractPlugin.loader,
                options: {
                  publicPath: "../",
                },
              },
              //   {
              //   // inject CSS to page
              //   loader: 'style-loader'
              // }, 
              {
                // translates CSS into CommonJS modules
                loader: 'css-loader'
              },

              {
                // Run postcss actions
                loader: 'postcss-loader',
                options: {
                  // `postcssOptions` is needed for postcss 8.x;
                  // if you use postcss 7.x skip the key
                  postcssOptions: {
                    // postcss plugins, can be exported to postcss.config.js
                    plugins: function () {
                      return [
                        require('autoprefixer')
                      ];
                    }
                  }
                }
              }, {
                // compiles Sass to CSS
                loader: 'sass-loader'
              }]
          },
          {
            test: /\.js$/,
            exclude: /(node_modules|bower_components)/,
            use: {
              loader: "babel-loader",
              options: {
                presets: ["@babel/preset-env"],
                plugins: ['@babel/plugin-transform-runtime']

              },
            },
          },
          {
            test: /\.(jpe?g|png|gif|svg)$/i,
            type: "asset",
          },
          // {
          //   test: /\.html$/i,
          //   type: "asset/resource",
          // },
          {
            test: /\.html$/i,
            loader: "html-loader",
            options: {
              minimize: true,

            }
          },
          {
            test: /\.tsx?$/,
            use: 'ts-loader',
            exclude: /node_modules/,
          }


        ],
      },
      plugins: [
        new GrpcToolsNodeProtocPlugin({
          protoPaths: [`${path.join(ComponentsPath, 'spotify/cspot/bell/external/nanopb/generator/proto')}`,
                      `${path.join(buildCRootPath, 'protobuf/proto')}`],
          protoSources: [`${path.join(buildCRootPath, 'protobuf/proto/*.proto')}`,
                        `${path.join(ComponentsPath, 'spotify/cspot/bell/external/nanopb/generator/proto/*.proto')}`],
          outputDir: './src/js/proto'
        }
        ),
        new HtmlWebpackPlugin({
          title: 'SqueezeESP32',
          template: './src/index.ejs',
          filename: 'index.html',
          inject: 'body',
          minify: {
            html5: true,
            collapseWhitespace: true,
            minifyCSS: true,
            minifyJS: true,
            minifyURLs: false,
            removeAttributeQuotes: true,
            removeComments: true, // false for Vue SSR to find app placeholder
            removeEmptyAttributes: true,
            removeOptionalTags: true,
            removeRedundantAttributes: true,
            removeScriptTypeAttributes: true,
            removeStyleLinkTypeAttributese: true,
            useShortDoctype: true
          },
          favicon: "./src/assets/images/favicon-32x32.png",
          excludeChunks: ['test'],
        }),
        // new CompressionPlugin({
        //   test: /\.(js|css|html|svg)$/,
        //   //filename: '[path].br[query]',
        //   filename: "[path][base].br",
        //   algorithm: 'brotliCompress',
        //   compressionOptions: { level: 11 },
        //   threshold: 100,
        //   minRatio: 0.8,
        //   deleteOriginalAssets: false
        // }),
        new MiniCssExtractPlugin({
          filename: "css/[name].css",
        }),
        new PurgeCSSPlugin({
          keyframes: false,
          paths: glob.sync(`${path.join(__dirname, 'src')}/**/*`, {
            nodir: true
          }),

          whitelist: whitelister('bootstrap/dist/css/bootstrap.css')
        }),
        new webpack.ProvidePlugin({
          $: "jquery",
          //          jQuery: "jquery",
          //        "window.jQuery": "jquery",
          //      Popper: ["popper.js", "default"],
          // Util: "exports-loader?Util!bootstrap/js/dist/util",
          // Dropdown: "exports-loader?Dropdown!bootstrap/js/dist/dropdown",
        }),
        new CompressionPlugin({
          //filename: '[path].gz[query]',
          test: /\.js$|\.css$|\.html$/,
          filename: "[path][base].gz",

          algorithm: 'gzip',

          threshold: 100,
          minRatio: 0.8,
        }),

      ],
      optimization: {
        minimize: true,
        providedExports: true,
        usedExports: true,
        minimizer: [

          new TerserPlugin({
            terserOptions: {
              format: {
                comments: false,
              },
            },
            extractComments: false,
            // enable parallel running
            parallel: true,
          }),
          new HtmlMinimizerPlugin({
            minimizerOptions: {
              removeComments: true,
              removeOptionalTags: true,

            }
          }
          ),
          new CssMinimizerPlugin(),
          new ImageMinimizerPlugin({
            minimizer: {
              implementation: ImageMinimizerPlugin.imageminMinify,
              options: {
                // Lossless optimization with custom option
                // Feel free to experiment with options for better result for you
                plugins: [
                  ["gifsicle", { interlaced: true }],
                  ["jpegtran", { progressive: true }],
                  ["optipng", { optimizationLevel: 5 }],
                  // Svgo configuration here https://github.com/svg/svgo#configuration
                  [
                    "svgo",
                    {
                      plugins: [
                        {
                          name: 'preset-default',
                          params: {
                            overrides: {
                              // customize default plugin options
                              inlineStyles: {
                                onlyMatchedOnce: false,
                              },

                              // or disable plugins
                              removeDoctype: false,
                            },
                          },
                        }
                      ],
                    },
                  ],
                ],
              },
            },
          }),


        ],
        splitChunks: {
          cacheGroups: {
            vendor: {
              name: "node_vendors",
              test: /[\\/]node_modules[\\/]/,
              chunks: "all",
            }
          }
        }

      },
      //   output: {
      //     filename: "[name].js",
      //     path: path.resolve(__dirname, "dist"),
      //     publicPath: "",
      //   },
      resolve: {
        extensions: ['.tsx', '.ts', '.js', '.ejs'],
      },
      output: {
        path: path.resolve(__dirname, 'dist'),
        // filename: './js/[name].[hash:6].bundle.js',
        filename: './js/[name].bundle.js',
        clean: true
      },
    }
  )
);