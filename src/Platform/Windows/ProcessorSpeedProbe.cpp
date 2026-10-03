#include "Platform/Windows/ProcessorSpeedProbe.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <cmath>

namespace tmpp::platform
{
    namespace
    {
        /// The counter this probe reads. The English name is required: PDH counter
        /// paths are localised on a non-English system, and this machine's counter set
        /// is English. A localisation-aware lookup would need PdhLookupPerfNameByIndex
        /// with the index of "% Processor Performance", which is a larger change and is
        /// noted in docs/METRICS.md.
        constexpr wchar_t const* COUNTER_PATH = L"\\Processor Information(_Total)\\% Processor Performance";

        [[nodiscard]] std::string _pdhError(PDH_STATUS status)
        {
            return "PDH status 0x" + [status] {
                char buffer[16]{};
                std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(status));
                return std::string{buffer};
            }();
        }
    }

    ProcessorSpeedProbe::ProcessorSpeedProbe(uint32_t baseClockMhz) : m_baseClockMhz(baseClockMhz)
    {
        if (m_baseClockMhz > 0)
        {
            _open();
        }
    }

    ProcessorSpeedProbe::~ProcessorSpeedProbe()
    {
        _close();
    }

    void ProcessorSpeedProbe::_open()
    {
        PDH_HQUERY query = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
        {
            return;
        }

        PDH_HCOUNTER counter = nullptr;
        PDH_STATUS const status = PdhAddEnglishCounterW(query, COUNTER_PATH, 0, &counter);
        if (status != ERROR_SUCCESS)
        {
            // The counter is absent on this system. Leave the handles null so
            // Available() reports false and the UI shows a blank instead of a figure.
            PdhCloseQuery(query);
            return;
        }

        // Prime the query: the counter is a rate, so the first collection establishes
        // the baseline the next one is compared against.
        PdhCollectQueryData(query);

        m_query = query;
        m_counter = counter;
        m_hasBaseline = true;
    }

    void ProcessorSpeedProbe::_close() noexcept
    {
        if (m_query != nullptr)
        {
            PdhCloseQuery(static_cast<PDH_HQUERY>(m_query));
            m_query = nullptr;
            m_counter = nullptr;
        }
    }

    Result<SystemProcessorSpeed> ProcessorSpeedProbe::Read()
    {
        SystemProcessorSpeed speed;

        if (!Available())
        {
            // Not an error at this level: the caller asked for a speed and the honest
            // answer is that this machine does not expose one. The Error path is
            // reserved for a counter that exists but failed to sample.
            return speed;
        }

        auto* const query = static_cast<PDH_HQUERY>(m_query);
        auto* const counter = static_cast<PDH_HCOUNTER>(m_counter);

        PDH_STATUS const collectStatus = PdhCollectQueryData(query);
        if (collectStatus != ERROR_SUCCESS)
        {
            return Error{ErrorCode::NativeFailure,
                         "PdhCollectQueryData failed with " + _pdhError(collectStatus),
                         "ProcessorSpeedProbe::Read"};
        }

        PDH_FMT_COUNTERVALUE value{};
        // The counter yields a percentage, so it is requested as a double rather than
        // as a large integer: the ratio has a fractional part and truncating it would
        // lose up to a full percent of the frequency.
        PDH_STATUS const readStatus = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value);
        if (readStatus != ERROR_SUCCESS)
        {
            // PDH_INVALID_DATA is expected on the very first sample after the query is
            // opened, since there is nothing to difference against yet. That is
            // reported as "no value yet", not as a failure.
            if (readStatus == static_cast<PDH_STATUS>(PDH_INVALID_DATA))
            {
                return speed;
            }
            return Error{ErrorCode::NativeFailure,
                         "PdhGetFormattedCounterValue failed with " + _pdhError(readStatus),
                         "ProcessorSpeedProbe::Read"};
        }

        if (value.CStatus != ERROR_SUCCESS && value.CStatus != static_cast<DWORD>(PDH_CSTATUS_VALID_DATA) &&
            value.CStatus != static_cast<DWORD>(PDH_CSTATUS_NEW_DATA))
        {
            // A counter can report that it has no valid data even when the read itself
            // succeeded, for example while the processor is parked.
            return speed;
        }

        double const performancePercent = value.doubleValue;
        if (!std::isfinite(performancePercent) || performancePercent <= 0.0)
        {
            return speed;
        }

        // The counter is a percentage of the rated clock, so the frequency is the
        // ratio applied to the base. Values above 100% are legitimate: they mean the
        // part is running above its rating (turbo).
        double const mhz = (static_cast<double>(m_baseClockMhz) * performancePercent) / 100.0;

        // Guard the cast: a bogus counter reading must not become a nonsense figure.
        if (mhz <= 0.0 || mhz > 20000.0)
        {
            return speed;
        }

        speed.currentMhz = static_cast<uint32_t>(std::lround(mhz));
        speed.available = true;
        return speed;
    }
}
