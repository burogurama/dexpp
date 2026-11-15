package io.github.burogurama.dexpp;

/**
 * How many entries each table of the written DEX file would have. A DEX file holds at most 65,535
 * types and prototypes, and at most 65,536 fields, methods, method handles and call sites. Use
 * these numbers to decide when a file is full.
 *
 * @param strings the number of strings
 * @param types the number of types; at most 65,535
 * @param protos the number of method prototypes; at most 65,535
 * @param fields the number of field identities; at most 65,536
 * @param methods the number of method identities; at most 65,536
 * @param methodHandles the number of method handles
 * @param callSites the number of call sites
 */
public record PoolStats(
        int strings,
        int types,
        int protos,
        int fields,
        int methods,
        int methodHandles,
        int callSites) {}
