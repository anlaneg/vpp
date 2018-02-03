#!/usr/bin/env python
#
# Copyright (c) 2016 Cisco and/or its affiliates.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import util
from string import Template

jvpp_ifc_template = Template("""
package $plugin_package;

/**
 * <p>Java representation of plugin's api file.
 * <br>It was generated by jvpp_impl_gen.py based on $inputfile
 * <br>(python representation of api file generated by vppapigen).
 */
public interface JVpp${plugin_name} extends $base_package.JVpp {

    /**
     * Generic dispatch method for sending requests to VPP
     *
     * @throws io.fd.vpp.jvpp.VppInvocationException if send request had failed
     */
    int send($base_package.$dto_package.JVppRequest request) throws io.fd.vpp.jvpp.VppInvocationException;

$methods
}
""")

jvpp_impl_template = Template("""
package $plugin_package;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Set;
import java.util.logging.Logger;
import java.util.logging.Level;
import $base_package.callback.JVppCallback;
import $base_package.VppConnection;
import $base_package.JVppRegistry;

/**
 * <p>Default implementation of JVpp interface.
 * <br>It was generated by jvpp_impl_gen.py based on $inputfile
 * <br>(python representation of api file generated by vppapigen).
 */
public final class JVpp${plugin_name}Impl implements $plugin_package.JVpp${plugin_name} {

    private final static Logger LOG = Logger.getLogger(JVpp${plugin_name}Impl.class.getName());
    private static final String LIBNAME = "libjvpp_${plugin_name_underscore}.so";

    // FIXME using NativeLibraryLoader makes load fail could not find (WantInterfaceEventsReply).
    static {
        try {
            loadLibrary();
        } catch (Exception e) {
            LOG.severe("Can't find jvpp jni library: " + LIBNAME);
            throw new ExceptionInInitializerError(e);
        }
    }

    private static void loadStream(final InputStream is) throws IOException {
        final Set<PosixFilePermission> perms = PosixFilePermissions.fromString("rwxr-x---");
        final Path p = Files.createTempFile(LIBNAME, null, PosixFilePermissions.asFileAttribute(perms));
        try {
            Files.copy(is, p, StandardCopyOption.REPLACE_EXISTING);

            try {
                Runtime.getRuntime().load(p.toString());
            } catch (UnsatisfiedLinkError e) {
                throw new IOException("Failed to load library " + p, e);
            }
        } finally {
            try {
                Files.deleteIfExists(p);
            } catch (IOException e) {
            }
        }
    }

    private static void loadLibrary() throws IOException {
        try (final InputStream is = JVpp${plugin_name}Impl.class.getResourceAsStream('/' + LIBNAME)) {
            if (is == null) {
                throw new IOException("Failed to open library resource " + LIBNAME);
            }
            loadStream(is);
        }
    }

    private VppConnection connection;
    private JVppRegistry registry;

    private static native void init0(final JVppCallback callback, final long queueAddress, final int clientIndex);
    @Override
    public void init(final JVppRegistry registry, final JVppCallback callback, final long queueAddress, final int clientIndex) {
        this.registry = java.util.Objects.requireNonNull(registry, "registry should not be null");
        this.connection = java.util.Objects.requireNonNull(registry.getConnection(), "connection should not be null");
        connection.checkActive();
        init0(callback, queueAddress, clientIndex);
    }

    private static native void close0();
    @Override
    public void close() {
        close0();
    }

    @Override
    public int send($base_package.$dto_package.JVppRequest request) throws io.fd.vpp.jvpp.VppInvocationException {
        return request.send(this);
    }

    @Override
    public final int controlPing(final io.fd.vpp.jvpp.dto.ControlPing controlPing) throws io.fd.vpp.jvpp.VppInvocationException {
        return registry.controlPing(JVpp${plugin_name}Impl.class);
    }

$methods
}
""")

method_template = Template("""    int $name($plugin_package.$dto_package.$request request) throws io.fd.vpp.jvpp.VppInvocationException;""")
method_native_template = Template(
    """    private static native int ${name}0($plugin_package.$dto_package.$request request);""")
method_impl_template = Template("""    public final int $name($plugin_package.$dto_package.$request request) throws io.fd.vpp.jvpp.VppInvocationException {
        java.util.Objects.requireNonNull(request,"Null request object");
        connection.checkActive();
        if(LOG.isLoggable(Level.FINE)) {
            LOG.fine(String.format("Sending $name event message: %s", request));
        }
        int result=${name}0(request);
        if(result<0){
            throw new io.fd.vpp.jvpp.VppInvocationException("${name}",result);
        }
        return result;
    }
""")

no_arg_method_template = Template("""    int $name() throws io.fd.vpp.jvpp.VppInvocationException;""")
no_arg_method_native_template = Template("""    private static native int ${name}0() throws io.fd.vpp.jvpp.VppInvocationException;""")
no_arg_method_impl_template = Template("""    public final int $name() throws io.fd.vpp.jvpp.VppInvocationException {
        connection.checkActive();
        LOG.fine("Sending $name event message");
        int result=${name}0();
        if(result<0){
            throw new io.fd.vpp.jvpp.VppInvocationException("${name}",result);
        }
        return result;
    }
""")


def generate_jvpp(func_list, base_package, plugin_package, plugin_name_underscore, dto_package, inputfile, logger):
    """ Generates JVpp interface and JNI implementation """
    logger.debug("Generating JVpp interface implementation for %s" % inputfile)
    plugin_name = util.underscore_to_camelcase_upper(plugin_name_underscore)

    methods = []
    methods_impl = []
    for func in func_list:
        camel_case_name = util.underscore_to_camelcase(func['name'])
        camel_case_name_upper = util.underscore_to_camelcase_upper(func['name'])
        if util.is_reply(camel_case_name):
            continue

        if len(func['args']) == 0:
            methods.append(no_arg_method_template.substitute(name=camel_case_name))
            methods_impl.append(no_arg_method_native_template.substitute(name=camel_case_name))
            methods_impl.append(no_arg_method_impl_template.substitute(name=camel_case_name))
        else:
            methods.append(method_template.substitute(name=camel_case_name,
                                                      request=camel_case_name_upper,
                                                      plugin_package=plugin_package,
                                                      dto_package=dto_package))
            methods_impl.append(method_native_template.substitute(name=camel_case_name,
                                                                  request=camel_case_name_upper,
                                                                  plugin_package=plugin_package,
                                                                  dto_package=dto_package))
            methods_impl.append(method_impl_template.substitute(name=camel_case_name,
                                                                request=camel_case_name_upper,
                                                                plugin_package=plugin_package,
                                                                dto_package=dto_package))

    jvpp_file = open("JVpp%s.java" % plugin_name, 'w')
    jvpp_file.write(
        jvpp_ifc_template.substitute(inputfile=inputfile,
                                     methods="\n".join(methods),
                                     base_package=base_package,
                                     plugin_package=plugin_package,
                                     plugin_name=plugin_name,
                                     dto_package=dto_package))
    jvpp_file.flush()
    jvpp_file.close()

    jvpp_file = open("JVpp%sImpl.java" % plugin_name, 'w')
    jvpp_file.write(jvpp_impl_template.substitute(inputfile=inputfile,
                                                  methods="\n".join(methods_impl),
                                                  base_package=base_package,
                                                  plugin_package=plugin_package,
                                                  plugin_name=plugin_name,
                                                  plugin_name_underscore=plugin_name_underscore,
                                                  dto_package=dto_package))
    jvpp_file.flush()
    jvpp_file.close()
