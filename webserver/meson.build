project('media-library-webserver', 
    'c', 
    'cpp', 
    version : '0.1.0',
    default_options : [ 'warning_level=1',
                        'buildtype=release',
                        'c_std=c11', 'cpp_std=c++20'])

cpp = meson.get_compiler('cpp')

# Add your source files here
sources = ['main.cpp', 
            'resources/encoder.cpp',
            'resources/osd.cpp',
            'resources/frontend.cpp',
            'resources/ai.cpp',
            'resources/repository.cpp',
            'pipeline/pipeline.cpp'
]

# Add any additional dependencies here
# Gstreamer Dependencies
gst_req = '>= 1.0.0'
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gst_base_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gst_app_dep = dependency('gstreamer-app-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_app_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'video_dep'])

gstreamer_deps = [gst_dep, gst_base_dep, gstvideo_dep, gst_app_dep]
dependencies = [dependency('threads'), dependency('glib-2.0'), gstreamer_deps]

executable('medialib-webserver',
  sources,
  dependencies: dependencies,
  gnu_symbol_visibility : 'default',
  cpp_args: ['-DHAVE_CONFIG_H', '-g', '-O0', '-DMEDIALIB_LOCAL_SERVER'],
  install: true,
  install_dir: get_option('bindir'),
)