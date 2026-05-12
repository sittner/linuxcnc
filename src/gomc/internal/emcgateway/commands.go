package emcgateway

// Command handlers have been moved to milltask (emccmd_handlers.cc).
// Commands are now dispatched directly through C callbacks registered
// by milltask via the gomc_api_t registry.  The gateway no longer needs
// to implement EmccmdCallbacks or use the NML command shim.
