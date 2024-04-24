#include "authorization_handler.hpp"

namespace authorization {
    /*
    AuthorizationHandler::AuthorizationHandler() {
        /**
         * TODO:
         *  1. init handler by 2. and 3.
         *  2. use the authZ policy parser to parse all access control policies from services /
         * accessControl namespace topic in configManager
         *  3. create a subscription for future access Control config changes

}
*/

    bool AuthorizationHandler::isAuthorized(std::string destination, Permission permission) {
        // TODO: check if destination service is registered with specific operation
        // TODO: if reg'd, check if permission exists in authZ module
        return true;
    }

} // namespace authorization
