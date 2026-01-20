#ifndef ATG_ENGINE_SIM_TRANSMISSION_NODE_H
#define ATG_ENGINE_SIM_TRANSMISSION_NODE_H

#include "object_reference_node.h"

#include "engine_sim.h"

#include <vector>

namespace es_script {

    class TransmissionNode : public ObjectReferenceNode<TransmissionNode> {
    public:
        TransmissionNode() { /* void */ }
        virtual ~TransmissionNode() { /* void */ }

        void generate(Transmission *transmission) const {
            Transmission::Parameters parameters = m_parameters;
            parameters.GearCount = static_cast<int>(m_gears.size());
            parameters.GearRatios = m_gears.data();

            transmission->initialize(parameters);
        }

        void addGear(double ratio) {
            m_gears.push_back(ratio);
        }

    protected:
        virtual void registerInputs() {
            addInput("max_clutch_torque", &m_parameters.MaxClutchTorque);

            // Compatibility inputs (used by some external/downloaded scripts).
            // Currently ignored by the core simulator.
            addInput("max_clutch_flex", &m_maxClutchFlex);
            addInput("limit_clutch_flex", &m_limitClutchFlex);
            addInput("clutch_stiffness", &m_clutchStiffness);
            addInput("clutch_damping", &m_clutchDamping);
            addInput("simulate_flex", &m_simulateFlex);

            ObjectReferenceNode<TransmissionNode>::registerInputs();
        }

        virtual void _evaluate() {
            setOutput(this);

            // Read inputs
            readAllInputs();
        }

        Transmission::Parameters m_parameters;
        std::vector<double> m_gears;

        // Compatibility-only ports (currently unused).
        double m_maxClutchFlex = 0.0;
        bool m_limitClutchFlex = false;
        double m_clutchStiffness = 0.0;
        double m_clutchDamping = 0.0;
        bool m_simulateFlex = false;
    };

} /* namespace es_script */

#endif /* ATG_ENGINE_SIM_TRANSMISSION_NODE_H */
