#ifndef ATG_ENGINE_SIM_VEHICLE_NODE_H
#define ATG_ENGINE_SIM_VEHICLE_NODE_H

#include "object_reference_node.h"

#include "engine_sim.h"

namespace es_script {

    class VehicleNode : public ObjectReferenceNode<VehicleNode> {
    public:
        VehicleNode() { /* void */ }
        virtual ~VehicleNode() { /* void */ }

        void generate(Vehicle *vehicle) const
        {
            vehicle->initialize(m_parameters);
        }

    protected:
        virtual void registerInputs() {
            addInput("mass", &m_parameters.mass);
            addInput("drag_coefficient", &m_parameters.dragCoefficient);
            addInput("cross_sectional_area", &m_parameters.crossSectionArea);
            addInput("diff_ratio", &m_parameters.diffRatio);
            addInput("tire_radius", &m_parameters.tireRadius);
            addInput("rolling_resistance", &m_parameters.rollingResistance);

            // Compatibility inputs (used by some external/downloaded scripts).
            // These are currently ignored by the core simulator, but accepting
            // them prevents "Port not found" compilation errors.
            addInput("stiffness", &m_stiffness);
            addInput("damping", &m_damping);
            addInput("max_flex", &m_maxFlex);
            addInput("limit_flex", &m_limitFlex);
            addInput("simulate_flex", &m_simulateFlex);
            addInput("max_brake_force", &m_maxBrakeForce);

            ObjectReferenceNode<VehicleNode>::registerInputs();
        }

        virtual void _evaluate() {           
            setOutput(this);
            // Read inputs
            readAllInputs();

        }

        Vehicle::Parameters m_parameters;

        // Compatibility-only ports (currently unused).
        double m_stiffness = 0.0;
        double m_damping = 0.0;
        double m_maxFlex = 0.0;
        bool m_limitFlex = false;
        bool m_simulateFlex = false;
        double m_maxBrakeForce = 0.0;
    };

} /* namespace es_script */

#endif /* ATG_ENGINE_SIM_VEHICLE_NODE_H */
