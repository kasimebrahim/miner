/*
 * opencog/embodiment/AutomatedSystemTest/PBTester.h
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * All Rights Reserved
 *
 * Written by Welter Luigi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _PB_TESTER_H
#define _PB_TESTER_H

#include <string>
#include <map>
#include <set>

#include <EmbodimentCogServer.h>
#include <Message.h>
#include "TestParameters.h"
#include "GoldStdGen.h"

namespace AutomatedSystemTest
{

class PBTester : public MessagingSystem::EmbodimentCogServer
{

private:

    TestParameters* testParams;
    bool failed;
    unsigned long numberOfReceivedMessages;

    void initialize();

    std::vector<MessagingSystem::Message*> expectedMessages;
    std::vector<unsigned long> receivedTimeMessages;

    PetaverseProxySimulator::GoldStdGen* goldStdGen;

public:

    static opencog::BaseServer* createInstance();

    PBTester();
    void init(TestParameters&);
    void init(const Control::SystemParameters &params, TestParameters& testParams, const std::string &myId, const std::string &ip, int portNumber);
    ~PBTester();

    bool processNextMessage(MessagingSystem::Message *message);
    void addExpectedMessage(MessagingSystem::Message* message, unsigned long time);
    bool hasExpectedMessages();
    void notifyEndOfGoldStdFile();
    PetaverseProxySimulator::GoldStdGen* getGoldStdGen();
    unsigned long getReceivedTimeOfCurrentExpectedMessage() {
        return receivedTimeMessages.back();
    }

}; // class
}  // namespace

#endif
