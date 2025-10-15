// ============= i_rest_api.h =============
#ifndef I_REST_API_H
#define I_REST_API_H

#include <string>

// Forward declaration
struct CHttpRequest;

// Interface for REST API Server
// Defines the contract that all REST API server implementations must follow
class IRestApiServer {
public:
    virtual ~IRestApiServer() = default;

    // Start the REST API server
    // Returns true on success, false on failure
    virtual bool Start() = 0;

    // Stop the REST API server
    // Gracefully shuts down all threads and connections
    virtual void Stop() = 0;

    // Check if the server is currently running
    // Returns true if running, false otherwise
    virtual bool IsRunning() const = 0;

    // Handle HTTP GET request
    // Parameters:
    //   str_endpoint - The URL path (e.g., "/chain", "/block")
    //   request - The full HTTP request object
    // Returns: JSON response string
    virtual std::string HandleGET(const std::string& str_endpoint, const CHttpRequest& request) = 0;

    // Handle HTTP POST request
    // Parameters:
    //   str_endpoint - The URL path (e.g., "/transaction", "/mine/start")
    //   request - The full HTTP request object
    // Returns: JSON response string
    virtual std::string HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) = 0;
};

#endif
