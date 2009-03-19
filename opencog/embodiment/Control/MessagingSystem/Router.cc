/**
 * Router.cc
 *
 * $Header$
 *
 * Author: Andre Senna
 * Creation: Fri Jun 22 15:07:50 BRT 2007
 */

#include <Sockets/SocketHandler.h>
#include <Sockets/ListenSocket.h>

#include <LoggerFactory.h>
#include <fstream>

#include "util/files.h"
#include "util/StringManipulator.h"

#include "Router.h"
#include "RouterServerSocket.h"
#include "NetworkElement.h"

using namespace MessagingSystem;
using namespace opencog;

bool Router::stopListenerThreadFlag = false;

Router::~Router() {
    if(!Router::stopListenerThreadFlag){
        Router::stopListenerThread();
    }
}

Router::Router(const Control::SystemParameters &params) : parameters(params) {
    
    running = true;
    lastNotifyTimestamp = 0; // this force router to send AVAILABLE_ELEMENT for all known NE as soon it starts up
    messageCentral = new MemoryMessageCentral();

    // NOTE: Uncomment the code to test a way to make router fail and raise
    //exception = false;

    RouterServerSocket::setMaster(this);
    this->routerId = this->parameters.get("ROUTER_ID");
    this->routerPort = atoi(this->parameters.get("ROUTER_PORT").c_str());
    this->routerAvailableNotificationInterval = atoi(parameters.get("ROUTER_AVAILABLE_NOTIFICATION_INTERVAL").c_str());
    this->noAckMessages = atoi(this->parameters.get("NO_ACK_MESSAGES").c_str()) == 1;

    //Nil: I comment that hopefully we don't need it
    //opencog::Logger::initMainLogger(Control::LoggerFactory::getLogger(this->parameters, routerId));
    
    pthread_mutex_init(&unavailableIdsLock, NULL);
    stopListenerThreadFlag = false;
}

void Router::startListener() {
    pthread_attr_init(&socketListenerAttr);
    pthread_attr_setscope(&socketListenerAttr, PTHREAD_SCOPE_PROCESS);
    pthread_attr_setinheritsched(&socketListenerAttr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setdetachstate(&socketListenerAttr, PTHREAD_CREATE_DETACHED);
    int errorCode = pthread_create(&socketListenerThread, &socketListenerAttr, Router::portListener, &routerPort);
    if (errorCode) {
        reportThreadError(errorCode);
    } else { 
        sleep(1); // Give time so that it starts listening server port before sending any message to NE's.
    }
}

void *Router::portListener(void *arg) {
    logger().log(opencog::Logger::DEBUG, "Router - Port listener executing.");

    int port = *((int*) arg);

    logger().log(opencog::Logger::INFO, "Router - Binding to port %d.", port);

    SocketHandler socketHandler;
    ListenSocket<RouterServerSocket> listenSocket(socketHandler);
	
    if (listenSocket.Bind(port)) {
        throw opencog::NetworkException(TRACE_INFO, "Router - Cannot bind to port %d.", port);
    }
	
    socketHandler.Add(&listenSocket);
    socketHandler.Select(0,200);

    logger().log(opencog::Logger::DEBUG, "Port listener ready.");

    while (!stopListenerThreadFlag) {
        if (socketHandler.GetCount() == 0) {
            throw opencog::NetworkException(TRACE_INFO, 
                      "Router - Bind to port %d is broken.", port);
        }
        socketHandler.Select(0,200);
    }

    logger().log(opencog::Logger::DEBUG, "Port listener finished.");
    return NULL;
}

void Router::stopListenerThread(){
    Router::stopListenerThreadFlag = true;
    // wait for thread to return
    pthread_join(socketListenerThread, NULL);
}

const Control::SystemParameters& Router::getParameters() const {
    return parameters;
}

void Router::run() {

    /**
     * Recovery operation (if needed). First mount the filename full path and
     * than check if such file exists in filesystem. If so, start recovery
     * process.
     */
    std::string recoveryFile = this->parameters.get("ROUTER_DATABASE_DIR");
    expandPath(recoveryFile);
    recoveryFile.append("/");
    recoveryFile.append(this->parameters.get("ROUTER_DATA_FILE"));

    if(fileExists(recoveryFile.c_str())){
        recoveryFromPersistedData(recoveryFile);
        remove(recoveryFile.c_str());
        startListener(); 
        notifyElementAvailability(routerId, false);
    } else {
        startListener(); 
    }


    do {
        // sleeps a bit since the only task to do here is availability notification to Network elements.
	// So, no need to be busy waiting all the time
        usleep(50000);

        // periodically router notification
        time_t now = time(NULL);

        if(now - lastNotifyTimestamp > routerAvailableNotificationInterval){
            notifyElementAvailability(routerId, true);
            lastNotifyTimestamp = now;
        }

        // copy set of elements whose availability notification is to be sent to a local variable
        std::set<std::string> localToNotifyAvailability; 
        pthread_mutex_lock(&unavailableIdsLock);
        for(std::set<std::string>::iterator it = Router::toNotifyAvailability.begin(); 
            it != Router::toNotifyAvailability.end(); it++){
            localToNotifyAvailability.insert(*it); 
        }
        Router::toNotifyAvailability.clear();
        pthread_mutex_unlock(&unavailableIdsLock);

        //logger().log(opencog::Logger::DEBUG, "Router - Checked pending availability notifications: %d", localToNotifyAvailability.size());

        // Now sends the available element notifications 
        if(!localToNotifyAvailability.empty()){
            for(std::set<std::string>::iterator it = localToNotifyAvailability.begin(); 
                it != localToNotifyAvailability.end(); it++){
                notifyElementAvailability(*it, true);
            }
        }


        // copy set of elements whose unavailability notification is to be sent to a local variable
        std::set<std::string> localToNotifyUnavailability; 
        pthread_mutex_lock(&unavailableIdsLock);
        for(std::set<std::string>::iterator it = Router::toNotifyUnavailability.begin(); 
            it != Router::toNotifyUnavailability.end(); it++){
            localToNotifyUnavailability.insert(*it); 
        }
        Router::toNotifyUnavailability.clear();
        pthread_mutex_unlock(&unavailableIdsLock);

        //logger().log(opencog::Logger::DEBUG, "Router - Checked pending unavailability notifications: %d", localToNotifyUnavailability.size());

        // Now sends the unavailable element notifications 
        if(!localToNotifyUnavailability.empty()){
            for(std::set<std::string>::iterator it = localToNotifyUnavailability.begin(); 
                it != localToNotifyUnavailability.end(); it++){
                notifyElementAvailability(*it, false);
            }
        }

        // NOTE: Uncomment the code to test a way to make router fail and raise
        // an execption
        //if (exception) {
        //    throw NetworkException(TRACE_INFO, "Router - Generating an exception.");
        //}

    } while(running);
}

void Router::shutdown(){
    running = false;

    // NOTE: Uncomment the code to test a way to make router fail and raise
    //exception = true;
}

// call-back interface

bool Router::knownID(const std::string &id) {

    std::map<std::string,int>::iterator iter1 = portNumber.find(id);
    if (iter1 == portNumber.end()) {
    	
    	// component not registered in router yet ... create it's message queue since the component
    	// may be starting up. 
		if (!messageCentral->existsQueue(id)) {
			messageCentral->createQueue(id);
		}
		return false;	
	} else {
		return true;
	}
}

MessageCentral* Router::getMessageCentral() {
	return messageCentral;	
}

int Router::getPortNumber(const std::string &id) {
    int port = portNumber[id];
    return port;
}

const std::string &Router::getIPAddress(const std::string &id) {
    const std::string &ip = ipAddress[id];
    return ip;
}

int Router::getControlSocket(const std::string &id) {
    int sock = -1;
    std::map<std::string,int>::iterator it = controlSockets.find(id); 
    if (it != controlSockets.end()) {
        sock = it->second;
    }
    //logger().log(opencog::Logger::DEBUG,"Getting control socket(%d) for element '%s'.", sock,  id.c_str()); 
    return sock;
}

int Router::getDataSocket(const std::string &id) {
    int sock = -1;
    std::map<std::string,int>::iterator it = dataSockets.find(id); 
    if (it != dataSockets.end()) {
        sock = it->second;
    }
    //logger().log(opencog::Logger::DEBUG,"Getting data socket(%d) for element '%s'.", sock,  id.c_str()); 
    return sock;
}

void Router::closeControlSocket(const std::string &id) {
    logger().log(opencog::Logger::DEBUG,"Closing control socket for element '%s'.", id.c_str()); 
    std::map<std::string,int>::iterator it = controlSockets.find(id); 
    if (it != controlSockets.end()) {
        int sock = it->second;
	close(sock);
	controlSockets.erase(id);
        logger().log(opencog::Logger::DEBUG,"Closed control socket: '%d'.", sock); 
    }
}

void Router::closeDataSocket(const std::string &id) {
    logger().log(opencog::Logger::DEBUG,"Closing data socket for element '%s'.", id.c_str()); 
    std::map<std::string,int>::iterator it = dataSockets.find(id); 
    if (it != dataSockets.end()) {
        int sock = it->second;
	close(sock);
	dataSockets.erase(id);
        logger().log(opencog::Logger::DEBUG,"Closed data socket: '%d'.", sock); 
    }
}

int Router::addNetworkElement(const std::string &strId, const std::string &strIp, int port) {

    std::string ip;
    std::string id;
    id.assign(strId);
    ip.assign(strIp);
    int errorCode = NO_ERROR;
       
	ipAddress[id] = ip;
    portNumber[id] = port;
    logger().log(opencog::Logger::DEBUG, 
        "Router - Adding component: '%s' - IP: '%s', Port: '%d'.", 
        id.c_str(), ipAddress[id].c_str(), port);
    
    // new data inserted, updates persisted router table
    try{
        persistState();
    } catch(opencog::IOException& e){ }

    // erease all messages from queue if have any 
    if(id == parameters.get("LS_ID") ||
       id == parameters.get("SPAWNER_ID")){

		messageCentral->createQueue(id, true);
    	errorCode = NO_ERROR;
    	
    // keep the messages within the queue, if any    
    } else {

		if (!messageCentral->existsQueue(id)) {    	
    		messageCentral->createQueue(id);
    		errorCode = NO_ERROR;
		} else {
			if (!messageCentral->isQueueEmpty(id)){
				errorCode = HAS_PENDING_MSGS;
			}
		}
    }

    // If the element has correctly executed the handshaking protocol and is
    // marked as an unavailable element (due to some previous crash or somethig
    // else), then the mark should be removed allowing others to communicate with 
    // the component.
    if ((errorCode == NO_ERROR || errorCode == HAS_PENDING_MSGS) && !isElementAvailable(id)) {
        markElementAvailable(id);
    }
    
    return errorCode;    
}

void Router::removeNetworkElement(const std::string &id) {

    std::string networkId;
    networkId.assign(id);

    ipAddress.erase(networkId);
    portNumber.erase(networkId);
    closeControlSocket(networkId);
    closeDataSocket(networkId);

    messageCentral->removeQueue(id);

    // save modifications into persisted data file
    try {
        persistState();
    } catch(opencog::IOException& e){ }
}

void Router::clearNetworkElementMessageQueue(const std::string &id) {
    messageCentral->clearQueue(id);
}

void Router::reportThreadError(const int &errorCode){

    std::string errorMsg;
    switch (errorCode) {
        case EAGAIN:
            errorMsg = "The system lacked the necessary resources to create another thread,"
                       " or the system-imposed limit on the total number of threads in a " 
                       "process PTHREAD_THREADS_MAX would be exceeded."; 
            break;
        case EINVAL:
            errorMsg = "The value specified by attr is invalid."; 
            break;
        case EPERM:
            errorMsg = "The caller does not have appropriate permission to set the required "
                       "scheduling parameters or scheduling policy.";
            break;
        default:
            errorMsg = "Unknown error code: ";
            errorMsg += opencog::toString(errorCode);  
            // When pthread_join is not called, this gets error code 12 after 380 created threads! 
    }
    logger().log(opencog::Logger::ERROR, 
                    "Thread error: \n%s.", errorMsg.c_str());
}

void Router::persistState(){
    
    // TODO: add some timestamp info to filename
    std::string path = parameters.get("ROUTER_DATABASE_DIR");
    expandPath(path);
    
    if(!createDirectory(path.c_str())){
        logger().log(opencog::Logger::ERROR, "Router - Cannot create directory '%s'.",
                        path.c_str());
        return;
    }
    
    // TODO: Insert timestamp information into routerInfo file
    std::string filename = path + "/" + parameters.get("ROUTER_DATA_FILE");
    remove(filename.c_str());

    std::ofstream routerFile(filename.c_str());
    routerFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try{

        std::map<std::string, std::string>::const_iterator it;
        for(it = ipAddress.begin(); it != ipAddress.end(); it++){
            
            std::string id = (*it).first;
            std::string ip = (*it).second;
            std::string port = opencog::toString(getPortNumber(id));

            // save data
            routerFile << id << " " << ip << " " << port  << std::endl;
        }
    }
    catch(std::ofstream::failure e){
        routerFile.close();
        throw opencog::IOException(TRACE_INFO, "Router - Unable to save Router information.");
    }
       
    routerFile.close();
}

void Router::recoveryFromPersistedData(const std::string& fileName){
    
    char line[256];
    std::ifstream fin(fileName.c_str());

    std::string id;
    std::string ip;
    int port;

    while (!fin.eof()){
        fin.getline(line, 256);

        // not a comentary or an empty line
        if(line[0] != '#' && line[0] != 0x00){
            std::istringstream in ((std::string)line);

            in >> id;
            in >> ip;
            in >> port;

            // reconstruct router table
            ipAddress[id] = ip;
            portNumber[id] = port;
            // since Router does not know about NE availability, mark them as unavailable 
            // until they handshake again.
            unavailableIds.insert(id);
        }
    }
    fin.close();   
}

void Router::notifyMessageArrival(const std::string& toId, unsigned int numMessages) {
    // the element to be notified is down, no need to spend system resources
    // trying to send a message
    if(isElementAvailable(toId) && dataSocketConnection(toId)){
        NotificationData data(toId, getDataSocket(toId), MESSAGE, "", numMessages);
        if (!sendNotification(data)) {
            closeDataSocket(toId);
            closeControlSocket(toId);
            markElementUnavailable(toId);
	}
    }
}

void Router::notifyElementAvailability(const std::string& id, bool available){
    logger().log(opencog::Logger::DEBUG, "Router::notifyElementAvailability(%s, %d)", id.c_str(), available);
    
    std::map<std::string, std::string>::const_iterator it;
    for(it = ipAddress.begin(); it != ipAddress.end(); it++){
        std::string toId = (*it).first;
        //logger().log(opencog::Logger::DEBUG, "Router - Check need for sending notification to %s", toId.c_str());

        // Prevent from sending a availability messages to the own element.
        if (toId == id) {
            //logger().log(opencog::Logger::DEBUG, "Router - Discarding notification to the own unavailable element");
            continue;
        } 
       
        // Notification messages are sent to all available elements and, when the notification refers to 
        // availability of the router itself, it sends the message even to the unavailable ones so that they 
        // have a chance to answer it and get removed from the unavailable_elements list.
        if ((isElementAvailable(toId) || (id == routerId) ) && controlSocketConnection(toId)){

            // Filtering element availability messages to Proxy. Only OPCs and
            // Router notifications should be sent.
            if( toId == parameters.get("PROXY_ID") &&
                (id == parameters.get("SPAWNER_ID") || 
                 id == parameters.get("LS_ID") ||
                 id == parameters.get("COMBO_SHELL_ID"))
              ){ 
                logger().log(opencog::Logger::DEBUG, "Router - Discarding notification from internal network elements to Proxy");
                continue;
            }
            
            NotificationType type;
            if(available){
                type = AVAILABLE;
            } else {
                type = UNAVAILABLE;
            }

            NotificationData data(toId, getControlSocket(toId), type, id, 0);
            if (!sendNotification(data)) {
                closeControlSocket(toId);
                closeDataSocket(toId);
                markElementUnavailable(toId);
	    } else {
                markElementAvailable(toId);
            }
        } else {
            logger().log(opencog::Logger::DEBUG, "Router - Discarding notification to element since it is unavailable (and reffered id is not router");
        }
    }
    //logger().log(opencog::Logger::DEBUG, "Router::notifyElementAvailability() ended");
}

bool Router::controlSocketConnection(const std::string& ne_id) {
  if ( controlSockets.find(ne_id) != controlSockets.end() ) {
    // Check if socket connection is still ok. If not so, remove the corresponding entry from controlSockets map and tries to re-connect.
    if(!isElementAvailable(ne_id)) {
        logger().log(opencog::Logger::INFO, 
                        "Router - controlSocketConnection(%s): Element marked as unavailable. Trying to re-connect...", ne_id.c_str() );
	closeControlSocket(ne_id);
        closeDataSocket(ne_id);
    } else {
        logger().log(opencog::Logger::DEBUG, 
                        "Router - controlSocketConnection(%s): Connection already established", ne_id.c_str() );
        return true;
    }
  } // if

  int sock; 
  if ( (sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot create a socket" );
  } // if
  
  logger().log(opencog::Logger::DEBUG,"Router - controlSocketConnection(%s): created new socket: %d.", ne_id.c_str(), sock); 

  int on = 1; // Keep connection alive
  if ( setsockopt ( sock, SOL_SOCKET, SO_REUSEADDR|SO_KEEPALIVE, ( const char* ) &on, sizeof ( on ) ) == -1 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot setup socket parameters" );
  } // if
#if 0 // TODO: This did not worked very well (got weird behaviors). This must be reviewed and fine tuned later.
  struct timeval tv;
  tv.tv_sec = 30;  // 30 Secs Timeout for receiving operation
  if ( setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval)) == -1 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot setup socket parameters" );
  } // if
#endif

  std::string ipAddr = getIPAddress(ne_id);
  int port = getPortNumber(ne_id);

  sockaddr_in client;
  client.sin_family = AF_INET;                  /* Internet/IP */
  client.sin_addr.s_addr = inet_addr(ipAddr.c_str());  /* IP address */
  client.sin_port = htons(port);       /* server port */

  if ( connect(sock, (sockaddr *) &client, sizeof(client)) >= 0 ) {
    logger().log(opencog::Logger::DEBUG, 
		    "Router - controlSocketConnection(%s). Connection established. ip=%s, port=%d", 
		    ne_id.c_str(), ipAddr.c_str(), port);
    controlSockets[ne_id] = sock;

    return true;
  } // if
  
  logger().log(opencog::Logger::ERROR, 
		  "Router - controlSocketConnection. Unable to connect to element %s. ip=%s, port=%d", 
		   ne_id.c_str(), ipAddr.c_str(), port);

  closeControlSocket(ne_id);
  closeDataSocket(ne_id);
  markElementUnavailable(ne_id);
  
  return false;
}

bool Router::dataSocketConnection(const std::string& ne_id) {
  if ( dataSockets.find(ne_id) != dataSockets.end() ) {
    // Check if socket connection is still ok. If not so, remove the corresponding entry from dataSockets map and tries to re-connect.
    if(!isElementAvailable(ne_id)) {
        logger().log(opencog::Logger::INFO, 
                        "Router - dataSocketConnection(%s): Element marked as unavailable. Trying to re-connect...", ne_id.c_str() );
	closeDataSocket(ne_id);
	closeControlSocket(ne_id);
    } else {
        logger().log(opencog::Logger::DEBUG, 
                        "Router - dataSocketConnection(%s): Connection already established", ne_id.c_str() );
        return true;
    }
  } // if

  int sock; 
  if ( (sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot create a socket" );
  } // if
  
  logger().log(opencog::Logger::DEBUG,"Router - dataSocketConnection(%s): created new socket: %d.", ne_id.c_str(), sock); 
  int on = 1; // keep connection alive
  if ( setsockopt ( sock, SOL_SOCKET, SO_REUSEADDR|SO_KEEPALIVE, ( const char* ) &on, sizeof ( on ) ) == -1 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot setup socket parameters" );
  } // if
#if 0 // TODO: This did not worked very well (got weird behaviors). This must be reviewed and fine tuned later.
  struct timeval tv;
  tv.tv_sec = 30;  // 30 Secs Timeout for receiving operation
  if ( setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval)) == -1 ) {
    throw opencog::NetworkException( TRACE_INFO, "Cannot setup socket parameters" );
  } // if
#endif

  std::string ipAddr = getIPAddress(ne_id);
  int port = getPortNumber(ne_id);

  sockaddr_in client;
  client.sin_family = AF_INET;                  /* Internet/IP */
  client.sin_addr.s_addr = inet_addr(ipAddr.c_str());  /* IP address */
  client.sin_port = htons(port);       /* server port */

  if ( connect(sock, (sockaddr *) &client, sizeof(client)) >= 0 ) {
    logger().log(opencog::Logger::DEBUG, 
		    "Router - dataSocketConnection(%s). Connection established. ip=%s, port=%d", 
		    ne_id.c_str(), ipAddr.c_str(), port);
    dataSockets[ne_id] = sock;

    return true;
  } // if
  
  logger().log(opencog::Logger::ERROR, 
		  "Router - dataSocketConnection. Unable to connect to element %s. ip=%s, port=%d", 
		   ne_id.c_str(), ipAddr.c_str(), port);

  closeDataSocket(ne_id);
  closeControlSocket(ne_id);
  markElementUnavailable(ne_id);
  
  return false;
}

bool Router::sendNotification(const NotificationData& data){
  logger().log(opencog::Logger::DEBUG, "Router - Sending notification.");

  try {
    std::string cmd;
    switch(data.type){
        case AVAILABLE:
            cmd.append("cAVAILABLE_ELEMENT ");
            cmd.append(data.element);
            cmd.append("\n");
            break;

        case UNAVAILABLE:
            cmd.append("cUNAVAILABLE_ELEMENT ");
            cmd.append(data.element);
            cmd.append("\n");
            break;

        case MESSAGE:
            cmd.append("cNOTIFY_NEW_MESSAGE ");
            cmd.append(opencog::toString(data.numMessages));
            cmd.append("\n");
            break;

        default:
        
            logger().log(opencog::Logger::ERROR, "Router - Invalid notification type '%d'.", 
                            (int)data.type);
            return false;
            break;
    }

    logger().log(opencog::Logger::DEBUG, "Router - Sending notification (socket = %d) '%s'.", data.sock, cmd.c_str());

    unsigned int sentBytes = 0;
    if ( ( sentBytes = send(data.sock, cmd.c_str(), cmd.length(), 0) ) != cmd.length() ) {
        logger().log(opencog::Logger::ERROR, "Router - sendNotification. Mismatch in number of sent bytes. %d was sent, but should be %d", sentBytes, cmd.length() );
	return false;
    }

    if (noAckMessages) {
        return true;
    }

#define BUFFER_SIZE 256
    char response[BUFFER_SIZE];
    int receivedBytes = 0;

    if ( (receivedBytes = recv(data.sock, response, BUFFER_SIZE-1, 0 ) ) <= 0 ) {
        logger().log(opencog::Logger::ERROR, "Router - sendNotification. Invalid response. recv returned %d ", receivedBytes );
        return false;
    }

    response[receivedBytes] = '\0'; // Assure null terminated string

    // chomp all trailing slashes from string
    int i;
    for ( i = receivedBytes-1; i >= 0 && (response[i] == '\n' || response[i] == '\r')  ; --i ) {    
        response[i] = '\0';
    }
  
    logger().log(opencog::Logger::DEBUG, "Router - sendNotification. Received response (after chomp): '%s' bytes: %d", response, receivedBytes );

    std::string answer = response;

    if(answer == NetworkElement::OK_MESSAGE){
        logger().log(opencog::Logger::DEBUG, "Router - Sucessfully sent notification to '%s'.",
                        data.toId.c_str());
	markElementAvailable(data.toId);
    } else {
        logger().log(opencog::Logger::ERROR, "Router - Failed to send notification to '%s'. (answer = %s)",
                        data.toId.c_str(), answer.c_str());
    }

  } catch(std::exception &e) {
    logger().log(opencog::Logger::ERROR, "Router::sendNotification - std::exception: %s", e.what());
  } catch(opencog::StandardException &e) {
    logger().log(opencog::Logger::ERROR, "Router::sendNotification - StandardException: %s", e.getMessage());
  } catch(...) {
    logger().log(opencog::Logger::ERROR, "Router::sendNotification - unknown exception!");
  }
  return true;
}

bool Router::isElementAvailable(const std::string &id){
    pthread_mutex_lock(&unavailableIdsLock);
    bool answer = (unavailableIds.find(id) == unavailableIds.end());
    pthread_mutex_unlock(&unavailableIdsLock);
    return answer;
}

void Router::markElementUnavailable(const std::string& ne_id) {
    pthread_mutex_lock(&unavailableIdsLock);
    Router::toNotifyUnavailability.insert(ne_id);
    unavailableIds.insert(ne_id);
    pthread_mutex_unlock(&unavailableIdsLock);
}

void Router::markElementAvailable(const std::string& ne_id) {
    pthread_mutex_lock(&unavailableIdsLock);
    if(unavailableIds.find(ne_id) != unavailableIds.end()){
        unavailableIds.erase(ne_id);
        Router::toNotifyAvailability.insert(ne_id);
    }
    pthread_mutex_unlock(&unavailableIdsLock);
}

