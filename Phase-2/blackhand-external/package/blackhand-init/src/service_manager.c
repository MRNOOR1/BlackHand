/*
 * Service manager stub.
 * Reads a service config file (see overlay/etc/blackhand/services.conf)
 * and starts services in dependency order.
 *
 * Suggested features:
 * - parse simple key=value format
 * - topological sort for dependencies
 * - restart policy (always / on-failure)
 */
