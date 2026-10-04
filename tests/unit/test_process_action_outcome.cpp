// Tests for reporting a process action's outcome to the user.
//
// Two of the outcomes are not failures, and saying so is the difference between a message the user
// can act on and one that looks like a bug: a process that exited between the list being drawn and
// the menu item being clicked is the ordinary race, and a system process refusing termination is a
// fact about the machine. Only the real faults carry an error.
//
// The formatting is what is tested, since the messages are what the user reads.
#include <gtest/gtest.h>

#include <string>

#include "Platform/Windows/WindowsProcessActions.h"

namespace tmpp::platform::test
{
    namespace
    {
        /// Mirrors the message the process list shows for an outcome.
        [[nodiscard]] std::string _messageFor(ProcessActionResult const& result, std::string const& subject)
        {
            if (result.Succeeded())
            {
                if (result.affected > 1)
                {
                    return std::to_string(result.affected) + " processes ended.";
                }
                return subject + " ended.";
            }
            return subject + ": " + result.message;
        }
    }

    TEST(ProcessActionOutcomeTest, ASingleSuccessNamesTheProcess)
    {
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::Succeeded;
        result.affected = 1;

        EXPECT_EQ(_messageFor(result, "notepad.exe"), "notepad.exe ended.");
    }

    TEST(ProcessActionOutcomeTest, ATreeSuccessReportsHowManyEnded)
    {
        // The user asked for one thing and several ended, so the count is the useful part.
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::Succeeded;
        result.affected = 7;

        EXPECT_EQ(_messageFor(result, "cmd.exe"), "7 processes ended.");
    }

    TEST(ProcessActionOutcomeTest, AnAlreadyExitedProcessIsNotReportedAsAFault)
    {
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::NotFound;
        result.message = "The process is no longer running.";

        std::string const message = _messageFor(result, "chrome.exe");

        // It must say what happened, not that something went wrong.
        EXPECT_NE(message.find("no longer running"), std::string::npos);
        EXPECT_FALSE(result.Succeeded()) << "it did not succeed, and the message must say why";
    }

    TEST(ProcessActionOutcomeTest, AccessDeniedTellsTheUserWhatWouldHelp)
    {
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::AccessDenied;
        result.message = "Access denied. This process needs administrator rights to end.";

        std::string const message = _messageFor(result, "System");

        EXPECT_NE(message.find("administrator"), std::string::npos)
            << "an access denial is actionable, so the action must be named";
    }

    TEST(ProcessActionOutcomeTest, AProtectedProcessSaysSoRatherThanFailingGenerically)
    {
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::Protected;
        result.message = "This is a system process and cannot be ended.";

        EXPECT_NE(_messageFor(result, "csrss.exe").find("cannot be ended"), std::string::npos);
    }

    TEST(ProcessActionOutcomeTest, APartialTreeReportsTheFailureNotTheSuccesses)
    {
        // Some of a tree ending and some refusing: the user has to be told about the part that did not
        // happen, or they will believe the tree is gone.
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::AccessDenied;
        result.affected = 5;
        result.failed = 1;
        result.message = "Access denied. This process needs administrator rights to end.";

        EXPECT_FALSE(result.Succeeded());
        EXPECT_NE(_messageFor(result, "explorer.exe").find("Access denied"), std::string::npos);
    }

    TEST(ProcessActionOutcomeTest, ASuccessCarriesNoMessage)
    {
        // Nothing to explain, so nothing is said: a success message would be noise.
        ProcessActionResult result;
        result.outcome = ProcessActionOutcome::Succeeded;
        result.affected = 1;

        EXPECT_TRUE(result.Succeeded());
        EXPECT_TRUE(result.message.empty());
    }
}
